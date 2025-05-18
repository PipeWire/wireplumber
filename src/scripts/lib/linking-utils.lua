-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of linking Lua utility functions

local cutils = require ("common-utils")

local lutils = {
  si_flags = {},
  priority_media_role_link = {},
}

function lutils.get_flags (self, si_id)
  if not self.si_flags [si_id] then
    self.si_flags [si_id] = {}
  end

  return self.si_flags [si_id]
end

function lutils.clear_flags (self, si_id)
  self.si_flags [si_id] = nil
end

function getprio (link)
  return tonumber (link.properties ["policy.role-based.priority"]) or 0
end

function getplugged (link)
  return tonumber (link.properties ["item.plugged.usec"]) or 0
end

function lutils.getAction (pmrl, link)
  local props = pmrl.properties

  if getprio (pmrl) == getprio (link) then
    return props ["policy.role-based.action.same-priority"] or "mix"
  else
    return props ["policy.role-based.action.lower-priority"] or "mix"
  end
end

-- if the link happens to be priority one, clear it and find the next
-- priority.
function lutils.clearPriorityMediaRoleLink (link)
  local lprops = link.properties
  local lmc = lprops ["target.media.class"]

  pmrl = lutils.getPriorityMediaRoleLink (lmc)

  -- only proceed if the link happens to be priority one.
  if pmrl ~= link then
    return
  end

  local prio_link = nil
  local prio = 0
  local plugged = 0
  for l in cutils.get_object_manager ("session-item"):iterate {
    type = "SiLink",
    Constraint { "item.factory.name", "=", "si-standard-link", type = "pw-global" },
    Constraint { "is.role.policy.link", "=", true },
    Constraint { "target.media.class", "=", lmc },
  } do
    local props = l.properties

    -- dont consider this link as it is about to be removed.
    if pmrl == l then
      goto continue
    end

    if getprio (link) > prio or
        (getprio (link) == prio and getplugged (link) > plugged) then
      prio = getprio (l)
      plugged = getplugged (l)
      prio_link = l
    end
    ::continue::
  end

  if prio_link then
    setPriorityMediaRoleLink (lmc, prio_link)
  else
    setPriorityMediaRoleLink (lmc, nil)
  end
end

-- record priority media role link
function lutils.updatePriorityMediaRoleLink (link)
  local lprops = link.properties
  local mc = lprops ["target.media.class"]

  if not lutils.priority_media_role_link [mc] then
    setPriorityMediaRoleLink (mc, link)
    return
  end

  pmrl = lutils.getPriorityMediaRoleLink (mc)

  if getprio (link) > getprio (pmrl) or
      (getprio (link) == getprio (pmrl) and getplugged (link) >= getplugged (pmrl)) then
    setPriorityMediaRoleLink (mc, link)
  end
end

function lutils.getPriorityMediaRoleLink (lmc)
  return lutils.priority_media_role_link [lmc]
end

function setPriorityMediaRoleLink (lmc, link)
  lutils.priority_media_role_link [lmc] = link
  if link then
    Log.debug (
      string.format ("update priority link(%d) media role(\"%s\") priority(%d)",
        link.id, link.properties ["media.role"], getprio (link)))
  else
    Log.debug ("clear priority media role")
  end
end

function lutils.is_role_policy_target (si_props, target_props)
  -- role-based policy links are those that link to targets with
  -- policy.role-based.target = true, unless the stream is a monitor
  -- (usually pavucontrol) or the stream is linking to the monitor ports
  -- of a sink (both are "input")
  return Core.test_feature ("hooks.linking.role-based.rescan")
      and cutils.parseBool (target_props["policy.role-based.target"])
      and not cutils.parseBool (si_props ["stream.monitor"])
      and si_props["item.node.direction"] ~= target_props["item.node.direction"]
end

function lutils.unwrap_select_target_event (self, event)
  local source = event:get_source ()
  local si = event:get_subject ()
  local target = event:get_data ("target")
  local om = source:call ("get-object-manager", "session-item")
  local si_id = si.id

  return source, om, si, si.properties, self:get_flags (si_id), target
end

function lutils.canPassthrough (si, si_target)
  local props = si.properties
  local tprops = si_target.properties
  -- both nodes must support encoded formats
  if not cutils.parseBool (props ["item.node.supports-encoded-fmts"])
      or not cutils.parseBool (tprops ["item.node.supports-encoded-fmts"]) then
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

function lutils.checkFollowDefault (si, si_target)
  -- If it got linked to the default target that is defined by node
  -- props but not metadata, start ignoring the node prop from now on.
  -- This is what Pulseaudio does.
  --
  -- Pulseaudio skips here filter streams (i->origin_sink and
  -- o->destination_source set in PA). Pipewire does not have a flag
  -- explicitly for this, but we can use presence of node.link-group.
  local si_props = si.properties
  local target_props = si_target.properties
  local reconnect = not cutils.parseBool (si_props ["node.dont-reconnect"])
  local is_filter = (si_props ["node.link-group"] ~= nil)

  if reconnect and not is_filter then
    local def_id = cutils.getDefaultNode (si_props,
      cutils.getTargetDirection (si_props))

    if target_props ["node.id"] == tostring (def_id) then
      local metadata = cutils.get_default_metadata_object ()
      -- Set target.node, for backward compatibility
      metadata:set (tonumber
        (si_props ["node.id"]), "target.node", "Spa:Id", "-1")
      Log.info (si, "... set metadata to follow default")
    end
  end
end

function lutils.lookupLink (si_id, si_target_id)
  local link = cutils.get_object_manager ("session-item"):lookup {
    type = "SiLink",
    Constraint { "out.item.id", "=", si_id },
    Constraint { "in.item.id", "=", si_target_id }
  }
  if not link then
    link = cutils.get_object_manager ("session-item"):lookup {
      type = "SiLink",
      Constraint { "in.item.id", "=", si_id },
      Constraint { "out.item.id", "=", si_target_id }
    }
  end
  return link
end

function lutils.isLinked (si_target)
  local target_id = si_target.id
  local linked = false
  local exclusive = false

  for l in cutils.get_object_manager ("session-item"):iterate {
    type = "SiLink",
  } do
    local p = l.properties
    local out_id = tonumber (p ["out.item.id"])
    local in_id = tonumber (p ["in.item.id"])
    linked = (out_id == target_id) or (in_id == target_id)
    if linked then
      exclusive = cutils.parseBool (p ["exclusive"]) or cutils.parseBool (p ["passthrough"])
      break
    end
  end
  return linked, exclusive
end

function lutils.getNodePeerId (node_id)
  for l in cutils.get_object_manager ("link"):iterate() do
    local p = l.properties
    local in_id = tonumber(p["link.input.node"])
    local out_id = tonumber(p["link.output.node"])
    if in_id == node_id then
      return out_id
    elseif out_id == node_id then
      return in_id
    end
  end
  return nil
end

function lutils.canLink (properties, si_target)
  local target_props = si_target.properties

  -- nodes must have the same media type
  if properties ["media.type"] ~= target_props ["media.type"] then
    return false
  end

  local function isMonitor(properties)
    return properties ["item.node.direction"] == "input" and
        cutils.parseBool (properties ["item.features.monitor"]) and
        not cutils.parseBool (properties ["item.features.no-dsp"]) and
        properties ["item.factory.name"] == "si-audio-adapter"
  end

  -- nodes must have opposite direction, or otherwise they must be both input
  -- and the target must have a monitor (so the target will be used as a source)
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
    for n in cutils.get_object_manager ("session-item"):iterate {
      type = "SiLinkable",
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "id", "!", si_target.id, type = "gobject" },
      Constraint { "item.node.direction", "!", target_props ["item.node.direction"] },
      Constraint { "node.link-group", "=", target_link_group },
    } do
      -- iterate their peers and return false if one of them cannot link
      for silink in cutils.get_object_manager ("session-item"):iterate {
        type = "SiLink",
      } do
        local out_id = tonumber (silink.properties ["out.item.id"])
        local in_id = tonumber (silink.properties ["in.item.id"])
        if out_id == n.id or in_id == n.id then
          local peer_id = (out_id == n.id) and in_id or out_id
          local peer = cutils.get_object_manager ("session-item"):lookup {
            type = "SiLinkable",
            Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
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

function lutils.findDefaultLinkable (si)
  local si_props = si.properties
  local target_direction = cutils.getTargetDirection (si_props)
  local def_node_id = cutils.getDefaultNode (si_props, target_direction)
  return cutils.get_object_manager ("session-item"):lookup {
    type = "SiLinkable",
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "node.id", "=", tostring (def_node_id) }
  }
end

function lutils.checkPassthroughCompatibility (si, si_target)
  local si_must_passthrough =
      cutils.parseBool (si.properties ["item.node.encoded-only"])
  local si_target_must_passthrough =
      cutils.parseBool (si_target.properties ["item.node.encoded-only"])
  local can_passthrough = lutils.canPassthrough (si, si_target)
  if (si_must_passthrough or si_target_must_passthrough)
      and not can_passthrough then
    return false, can_passthrough
  end
  return true, can_passthrough
end

-- If the node has an associated device, does it have any active/available
-- paths/routes to the physical device?
-- Some UCM profiles expose all paths (headphones, HDMI, etc) as nodes,
-- even though they may not be connected... See #145
function lutils.haveAvailableRoutes (si_props, devices_om)
  local card_profile_device = si_props ["card.profile.device"]
  local device_id = si_props ["device.id"]

  -- if the node does not have an associated device, it's all good
  if not card_profile_device or not device_id then
    return true
  end

  -- Get the device
  devices_om = devices_om or cutils.get_object_manager ("device")
  local device = devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }

  if not device then
    return true
  end

  -- Check if at least one of the current device routes supports
  -- the node's card.profile.device and is available
  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if route and (route.device == tonumber (card_profile_device)) then
      if (route.available == "no") then
        return false
      else
        return true
      end
    end
  end

  -- Check if available routes support the node's card.profile.device
  local found = 0
  for r in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (p, "EnumRoute")
    if route and type (route.devices) == "table" then
      for _, i in ipairs (route.devices) do
        if i == tonumber (cpd) then
          found = found + 1
          if route.available ~= "no" then
            return true
          end
        end
      end
    end
  end

  -- The node is part of a profile without routes so we assume it
  -- is available. This can happen for Pro Audio profiles
  if found == 0 then
    return true
  end

  return false
end

function lutils.sendClientError (event, node, code, message)
  local source = event:get_source ()
  local client_id = node.properties ["client.id"]
  if client_id then
    local clients_om = source:call ("get-object-manager", "client")
    local client = clients_om:lookup {
        Constraint { "bound-id", "=", client_id, type = "gobject" }
    }
    if client then
      client:send_error (node ["bound-id"], code, message)
    end
  end
end

return lutils
