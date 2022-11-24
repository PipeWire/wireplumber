-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of policy Lua utility functions

local cutils = require ("common-utils")

function parseBool (var)
  return cutils.parseBool (var)
end

local putils = {
  si_flags = {},
}

function putils.get_flags (self, si_id)
  if not self.si_flags [si_id] then
    self.si_flags [si_id] = {}
  end

  return self.si_flags [si_id]
end

function putils.clear_flags (self, si_id)
  self.si_flags [si_id] = nil
end

function putils.unwrap_find_target_event (self, event)
  local source = event:get_source ()
  local si = event:get_subject ()
  local target = event:get_data ("target")
  local om = source:call ("get-object-manager", "session-item")
  local si_id = si.id

  return source, om, si, si.properties, self:get_flags (si_id), target
end

function putils.canPassthrough (si, si_target)
  local props = si.properties
  local tprops = si_target.properties
  -- both nodes must support encoded formats
  if not parseBool (props ["item.node.supports-encoded-fmts"])
      or not parseBool (tprops ["item.node.supports-encoded-fmts"]) then
    return false
  end

  -- make sure that the nodes have at least one common non-raw format
  local n1 = si:get_associated_proxy ("node")
  local n2 = si_target:get_associated_proxy ("node")
  for p1 in n1:iterate_params ("EnumFormat") do
    local p1p = p1:parse ()
    if p1p.properties.mediaSubtype ~= "raw" then
      for p2 in n2:iterate_params ("EnumFormat") do
        if p1:filter (p2) then
          return true
        end
      end
    end
  end
  return false
end

function putils.checkFollowDefault (si, si_target, has_node_defined_target)
  -- If it got linked to the default target that is defined by node
  -- props but not metadata, start ignoring the node prop from now on.
  -- This is what Pulseaudio does.
  --
  -- Pulseaudio skips here filter streams (i->origin_sink and
  -- o->destination_source set in PA). Pipewire does not have a flag
  -- explicitly for this, but we can use presence of node.link-group.
  if not has_node_defined_target then
    return
  end
  local si_props = si.properties
  local target_props = si_target.properties
  local reconnect = not parseBool (si_props ["node.dont-reconnect"])
  local is_filter = (si_props ["node.link-group"] ~= nil)

  if follow and default_nodes ~= nil and reconnect and not is_filter then
    local def_id = getDefaultNode (si_props,
      cutils.getTargetDirection (si_props))

    if target_props ["node.id"] == tostring (def_id) then
      local metadata = putils.get_default_metadata_object ()
      -- Set target.node, for backward compatibility
      metadata:set (tonumber
        (si_props ["node.id"]), "target.node", "Spa:Id", "-1")
      Log.info (si, "... set metadata to follow default")
    end
  end
end

function putils.lookupLink (si_id, si_target_id)
  local link = links_om:lookup {
    Constraint { "out.item.id", "=", si_id },
    Constraint { "in.item.id", "=", si_target_id }
  }
  if not link then
    link = links_om:lookup {
      Constraint { "in.item.id", "=", si_id },
      Constraint { "out.item.id", "=", si_target_id }
    }
  end
  return link
end

function putils.isLinked (si_target)
  local target_id = si_target.id
  local linked = false
  local exclusive = false

  for l in links_om:iterate () do
    local p = l.properties
    local out_id = tonumber (p ["out.item.id"])
    local in_id = tonumber (p ["in.item.id"])
    linked = (out_id == target_id) or (in_id == target_id)
    if linked then
      exclusive = parseBool (p ["exclusive"]) or parseBool (p ["passthrough"])
      break
    end
  end
  return linked, exclusive
end

function putils.canLink (properties, si_target)
  local target_props = si_target.properties

  -- nodes must have the same media type
  if properties ["media.type"] ~= target_props ["media.type"] then
    return false
  end

  -- nodes must have opposite direction, or otherwise they must be both input
  -- and the target must have a monitor (so the target will be used as a source)
  local function isMonitor(properties)
    return properties ["item.node.direction"] == "input" and
        parseBool (properties ["item.features.monitor"]) and
        not parseBool (properties ["item.features.no-dsp"]) and
        properties ["item.factory.name"] == "si-audio-adapter"
  end

  if properties ["item.node.direction"] == target_props ["item.node.direction"]
      and not isMonitor (target_props) then
    return false
  end

  -- check link group
  local function canLinkGroupCheck(link_group, si_target, hops)
    local target_props = si_target.properties
    local target_link_group = target_props ["node.link-group"]

    if hops == 8 then
      return false
    end

    -- allow linking if target has no link-group property
    if not target_link_group then
      return true
    end

    -- do not allow linking if target has the same link-group
    if link_group == target_link_group then
      return false
    end

    -- make sure target is not linked with another node with same link group
    -- start by locating other nodes in the target's link-group, in opposite direction
    for n in linkables_om:iterate {
      Constraint { "id", "!", si_target.id, type = "gobject" },
      Constraint { "item.node.direction", "!", target_props ["item.node.direction"] },
      Constraint { "node.link-group", "=", target_link_group },
    } do
      -- iterate their peers and return false if one of them cannot link
      for silink in links_om:iterate () do
        local out_id = tonumber (silink.properties ["out.item.id"])
        local in_id = tonumber (silink.properties ["in.item.id"])
        if out_id == n.id or in_id == n.id then
          local peer_id = (out_id == n.id) and in_id or out_id
          local peer = linkables_om:lookup {
            Constraint { "id", "=", peer_id, type = "gobject" },
          }
          if peer and not canLinkGroupCheck (link_group, peer, hops + 1) then
            return false
          end
        end
      end
    end
    return true
  end

  local link_group = properties ["node.link-group"]
  if link_group then
    return canLinkGroupCheck (link_group, si_target, 0)
  end
  return true
end

function putils.findDefaultLinkable (si)
  local si_props = si.properties
  local target_direction = cutils.getTargetDirection (si_props)
  local def_node_id = cutils.getDefaultNode (si_props, target_direction)
  return linkables_om:lookup {
    Constraint { "node.id", "=", tostring (def_node_id) }
  }
end

function putils.checkPassthroughCompatibility (si, si_target)
  local si_must_passthrough =
  parseBool (si.properties ["item.node.encoded-only"])
  local si_target_must_passthrough =
  parseBool (si_target.properties ["item.node.encoded-only"])
  local can_passthrough = putils.canPassthrough (si, si_target)
  if (si_must_passthrough or si_target_must_passthrough)
      and not can_passthrough then
    return false, can_passthrough
  end
  return true, can_passthrough
end

-- Does the target device have any active/available paths/routes to
-- the physical device(spkr/mic/cam)?
function putils.haveAvailableRoutes (si_props)
  local card_profile_device = si_props ["card.profile.device"]
  local device_id = si_props ["device.id"]
  local device = device_id and devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }

  if not card_profile_device or not device then
    return true
  end

  local found = 0
  local avail = 0

  -- First check "SPA_PARAM_Route" if there are any active devices
  -- in an active profile.
  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if not route then
      goto skip_route
    end

    if (route.device ~= tonumber (card_profile_device)) then
      goto skip_route
    end

    if (route.available == "no") then
      return false
    end

    do return true end

    ::skip_route::
  end

  -- Second check "SPA_PARAM_EnumRoute" if there is any route that
  -- is available if not active.
  for p in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if not route then
      goto skip_enum_route
    end

    if not cutils.arrayContains
          (route.devices, tonumber (card_profile_device)) then
      goto skip_enum_route
    end
    found = found + 1;
    if (route.available ~= "no") then
      avail = avail + 1
    end
    ::skip_enum_route::
  end

  if found == 0 then
    return true
  end
  if avail > 0 then
    return true
  end

  return false
end

devices_om = ObjectManager { Interest { type = "device" } }

devices_om:activate ()

linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    -- only handle si-audio-adapter and si-node
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "active-features", "!", 0, type = "gobject" },
  }
}

linkables_om:activate ()

links_om = ObjectManager {
  Interest {
    type = "SiLink",
    -- only handle links created by this policy
    Constraint { "is.policy.item.link", "=", true },
  }
}

links_om:activate ()

function putils.get_default_metadata_object ()
  return cutils.default_metadata_om:lookup ()
end

return putils
