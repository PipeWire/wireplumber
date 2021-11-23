-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- Receive script arguments from config.lua
local config = ...

-- ensure config.move and config.follow are not nil
config.move = config.move or false
config.follow = config.follow or false

local pending_rescan = false

function parseBool(var)
  return var and (var:lower() == "true" or var == "1")
end

function createLink (si, si_target, passthrough, exclusive)
  local out_item = nil
  local in_item = nil
  local si_props = si.properties
  local target_props = si_target.properties
  local si_id = si.id

  -- break rescan if tried more than 5 times with same target
  if si_flags[si_id].failed_peer_id ~= nil and
      si_flags[si_id].failed_peer_id == si_target.id and
      si_flags[si_id].failed_count ~= nil and
      si_flags[si_id].failed_count > 5 then
    Log.warning (si, "tried to link on last rescan, not retrying")
    return
  end

  if si_props["item.node.direction"] == "output" then
    -- playback
    out_item = si
    in_item = si_target
  else
    -- capture
    in_item = si
    out_item = si_target
  end

  local passive = parseBool(si_props["node.passive"]) or
      parseBool(target_props["node.passive"])

  Log.info (
    string.format("link %s <-> %s passive:%s, passthrough:%s, exclusive:%s",
      tostring(si_props["node.name"]),
      tostring(target_props["node.name"]),
      tostring(passive), tostring(passthrough), tostring(exclusive)))

  -- create and configure link
  local si_link = SessionItem ( "si-standard-link" )
  if not si_link:configure {
    ["out.item"] = out_item,
    ["in.item"] = in_item,
    ["passive"] = passive,
    ["passthrough"] = passthrough,
    ["exclusive"] = exclusive,
    ["out.item.port.context"] = "output",
    ["in.item.port.context"] = "input",
    ["is.policy.item.link"] = true,
  } then
    Log.warning (si_link, "failed to configure si-standard-link")
    return
  end

  -- register
  si_flags[si_id].peer_id = si_target.id
  si_flags[si_id].failed_peer_id = si_target.id
  if si_flags[si_id].failed_count ~= nil then
    si_flags[si_id].failed_count = si_flags[si_id].failed_count + 1
  else
    si_flags[si_id].failed_count = 1
  end
  si_link:register ()

  -- activate
  si_link:activate (Feature.SessionItem.ACTIVE, function (l, e)
    if e then
      Log.warning (l, "failed to activate si-standard-link: " .. tostring(e))
      si_flags[si_id].peer_id = nil
      l:remove ()
    else
      si_flags[si_id].failed_peer_id = nil
      si_flags[si_id].failed_count = 0
      Log.info (l, "activated si-standard-link")
    end
  end)
end

function isLinked(si_target)
  local target_id = si_target.id
  local linked = false
  local exclusive = false

  for l in links_om:iterate() do
    local p = l.properties
    local out_id = tonumber(p["out.item.id"])
    local in_id = tonumber(p["in.item.id"])
    linked = (out_id == target_id) or (in_id == target_id)
    if linked then
      exclusive = parseBool(p["exclusive"]) or parseBool(p["passthrough"])
      break
    end
  end
  return linked, exclusive
end

function canPassthrough (si, si_target)
  -- both nodes must support encoded formats
  if not parseBool(si.properties["item.node.supports-encoded-fmts"])
      or not parseBool(si_target.properties["item.node.supports-encoded-fmts"]) then
    return false
  end

  -- make sure that the nodes have at least one common non-raw format
  local n1 = si:get_associated_proxy ("node")
  local n2 = si_target:get_associated_proxy ("node")
  for p1 in n1:iterate_params("EnumFormat") do
    local p1p = p1:parse()
    if p1p.properties.mediaSubtype ~= "raw" then
      for p2 in n2:iterate_params("EnumFormat") do
        if p1:filter(p2) then
          return true
        end
      end
    end
  end
  return false
end

function canLink (properties, si_target)
  local target_properties = si_target.properties

  -- nodes must have the same media type
  if properties["media.type"] ~= target_properties["media.type"] then
    return false
  end

  -- nodes must have opposite direction, or otherwise they must be both input
  -- and the target must have a monitor (so the target will be used as a source)
  local function isMonitor(properties)
    return properties["item.node.direction"] == "input" and
          parseBool(properties["item.features.monitor"]) and
          not parseBool(properties["item.features.no-dsp"]) and
          properties["item.factory.name"] == "si-audio-adapter"
  end

  if properties["item.node.direction"] == target_properties["item.node.direction"]
      and not isMonitor(target_properties) then
    return false
  end

  -- check link group
  local function canLinkGroupCheck (link_group, si_target, hops)
    local target_props = si_target.properties
    local target_link_group = target_props["node.link-group"]

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
      Constraint { "item.node.direction", "!", target_props["item.node.direction"] },
      Constraint { "node.link-group", "=", target_link_group },
    } do
      -- iterate their peers and return false if one of them cannot link
      for silink in links_om:iterate() do
        local out_id = tonumber(silink.properties["out.item.id"])
        local in_id = tonumber(silink.properties["in.item.id"])
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

  local link_group = properties["node.link-group"]
  if link_group then
    return canLinkGroupCheck (link_group, si_target, 0)
  end
  return true
end

function getTargetDirection(properties)
  local target_direction = nil
  if properties["item.node.direction"] == "output" or
     (properties["item.node.direction"] == "input" and
        parseBool(properties["stream.capture.sink"])) then
    target_direction = "input"
  else
    target_direction = "output"
  end
  return target_direction
end

function getDefaultNode(properties, target_direction)
  local target_media_class =
        properties["media.type"] ..
        (target_direction == "input" and "/Sink" or "/Source")
  return default_nodes:call("get-default-node", target_media_class)
end

-- Try to locate a valid target node that was explicitly requsted by the
-- client(node.target) or by the user(target.node)
-- Use the target.node metadata, if config.move is enabled,
-- then use the node.target property that was set on the node
-- `properties` must be the properties dictionary of the session item
-- that is currently being handled
function findDefinedTarget (properties)
  local metadata = config.move and metadata_om:lookup()
  local target_id = metadata
      and metadata:find(properties["node.id"], "target.node")
      or properties["node.target"]
  local target_direction = getTargetDirection(properties)

  if target_id and tonumber(target_id) then
    local si_target = linkables_om:lookup {
      Constraint { "node.id", "=", target_id },
    }
    if si_target and canLink (properties, si_target) then
      return si_target
    end
  end

  if target_id then
    for si_target in linkables_om:iterate() do
      local target_props = si_target.properties
      if (target_props["node.name"] == target_id or
          target_props["object.path"] == target_id) and
          target_props["item.node.direction"] == target_direction and
          canLink (properties, si_target) then
        return si_target
      end
    end
  end
  return nil
end

-- User or client defined target is not available, Loop through all the valid
-- linkables(nodes) and pick an appropriate one.
function findUndefinedTarget (si)
  local si_props = si.properties
  local target_direction = getTargetDirection(si_props)
  local target_picked = nil
  local target_can_passthrough = false
  local target_priority = 0
  local target_plugged = 0

  for si_target in linkables_om:iterate {
    Constraint { "item.node.type", "=", "device" },
    Constraint { "item.node.direction", "=", target_direction },
    Constraint { "media.type", "=", si_props["media.type"] },
  } do
    local si_target_props = si_target.properties
    local si_target_node_id = si_target_props["node.id"]

    Log.debug(string.format("Looking at: %s (%s)",
        tostring(si_target_props["node.name"]),
        tostring(si_target_node_id)))

    if not canLink (si_props, si_target) then
      Log.debug("... cannot link, skip linkable")
      goto skip_linkable
    end

    local priority = tonumber(si_target_props["priority.session"]) or 0

    -- Is this linkable(node) a default one?
    local def_node_id = getDefaultNode(si_props, target_direction)
    if tostring(def_node_id) == si_target_node_id then
      Log.debug(string.format("... this (%s) is the default node for %s",
          def_node_id, target_direction))
      priority = priority + 10000
    end

    -- todo:check if this linkable(node/device) have valid routes.

    -- Is passthrough feasible between these linkables(nodes)?
    local si_must_passthrough = parseBool(si_props["item.node.encoded-only"])
    local si_target_must_passthrough = parseBool(si_target_props["item.node.encoded-only"])
    local can_passthrough = canPassthrough(si, si_target)

    if (si_must_passthrough or si_target_must_passthrough)
        and not can_passthrough then
      Log.debug(string.format("... cannot passthrough, skip; must:%s target_must:%s",
          tostring(si_must_passthrough),
          tostring(si_target_must_passthrough)))
      goto skip_linkable
    end

    local plugged = tonumber(si_target_props["item.plugged.usec"]) or 0

    Log.debug("... priority:"..tostring(priority)..", plugged:"..tostring(plugged))

    -- (target_picked == NULL) --> make sure atleast one target is picked.
    -- (priority > target_priority) --> pick the highest priority linkable(node)
    -- target.
    -- (priority == target_priority and plugged > target_plugged) --> pick the
    -- latest connected/plugged(in time) linkable(node) target.
    if (target_picked == nil or
        priority > target_priority or
        (priority == target_priority and plugged > target_plugged)) then
          Log.debug("... picked")
          target_picked = si_target
          target_can_passthrough = can_passthrough
          target_priority = priority
          target_plugged = plugged
    end
    ::skip_linkable::
  end

  if target_picked then
    Log.info(string.format("... target: %s (%s), can_passthrough:%s",
      tostring(target_picked.properties["node.name"]),
      tostring(target_picked.properties["node.id"]),
      tostring(target_can_passthrough)))
    return target_picked, target_can_passthrough
  else
    return nil, nil
  end

end

function lookupLink (si_id, si_target_id)
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

function checkLinkable(si)
  -- only handle stream session items
  local si_props = si.properties
  if not si_props or si_props["item.node.type"] ~= "stream" then
    return false
  end

  -- Determine if we can handle item by this policy
  local media_role = si_props["media.role"]
  if endpoints_om:get_n_objects () > 0 and media_role ~= nil then
    return false
  end

  return true, si_props
end

si_flags = {}

function handleLinkable (si)
  local valid, si_props = checkLinkable(si)
  if not valid then
    return
  end

  -- check if we need to link this node at all
  local autoconnect = parseBool(si_props["node.autoconnect"])
  if not autoconnect then
    Log.debug (si, tostring(si_props["node.name"]) .. " does not need to be autoconnected")
    return
  end

  Log.info (si, string.format("handling item: %s (%s)",
      tostring(si_props["node.name"]), tostring(si_props["node.id"])))

  -- prepare flags table
  if not si_flags[si.id] then
    si_flags[si.id] = {}
  end

  -- get other important node properties
  local reconnect = not parseBool(si_props["node.dont-reconnect"])
  local exclusive = parseBool(si_props["node.exclusive"])
  local si_must_passthrough = parseBool(si_props["item.node.encoded-only"])

  -- find defined target
  local si_target = findDefinedTarget(si_props)
  local can_passthrough = si_target and canPassthrough(si, si_target)

  if si_target and si_must_passthrough and not can_passthrough then
    si_target = nil
  end

  -- wait up to 2 seconds for the requested target to become available
  -- this is because the client may have already "seen" a target that we haven't
  -- yet prepared, which leads to a race condition
  local si_id = si.id
  if si_props["node.target"] and si_props["node.target"] ~= "-1"
      and not si_target
      and not si_flags[si_id].was_handled
      and not si_flags[si_id].done_waiting then
    if not si_flags[si_id].timeout_source then
      si_flags[si_id].timeout_source = Core.timeout_add(2000, function()
        if si_flags[si_id] then
          si_flags[si_id].done_waiting = true
          si_flags[si_id].timeout_source = nil
          rescan()
        end
        return false
      end)
    end
    Log.info (si, "... waiting for target")
    return
  end

  -- find fallback target
  if not si_target then
    si_target, can_passthrough = findUndefinedTarget(si)
  end

  -- Check if item is linked to proper target, otherwise re-link
  if si_flags[si_id].peer_id then
    if si_target and si_flags[si_id].peer_id == si_target.id then
      Log.debug (si, "... already linked to proper target")
      return
    end
    if reconnect then
      local link = lookupLink (si_id, si_flags[si_id].peer_id)
      if link ~= nil then
        -- remove old link if active, otherwise schedule rescan
        if ((link:get_active_features() & Feature.SessionItem.ACTIVE) ~= 0) then
          si_flags[si_id].peer_id = nil
          link:remove ()
          Log.info (si, "... moving to new target")
        else
          pending_rescan = true
          Log.info (si, "... scheduled rescan")
          return
        end
      end
    end
  end

  -- if the stream has dont-reconnect and was already linked before,
  -- don't link it to a new target
  if not reconnect and si_flags[si.id].was_handled then
    si_target = nil
  end

  -- check target's availability
  if si_target then
    local target_is_linked, target_is_exclusive = isLinked(si_target)
    if target_is_exclusive then
      Log.info(si, "... target is linked exclusively")
      si_target = nil
    end

    if target_is_linked then
      if exclusive or si_must_passthrough then
        Log.info(si, "... target is already linked, cannot link exclusively")
        si_target = nil
      else
        -- disable passthrough, we can live without it
        can_passthrough = false
      end
    end
  end

  if not si_target then
    Log.info (si, "... target not found, reconnect:" .. tostring(reconnect))

    local node = si:get_associated_proxy ("node")
    if not reconnect then
      Log.info (si, "... destroy node")
      node:request_destroy()
    elseif si_flags[si.id].was_handled then
      Log.info (si, "... waiting reconnect")
      return
    end

    local client_id = node.properties["client.id"]
    if client_id then
      local client = clients_om:lookup {
        Constraint { "bound-id", "=", client_id, type = "gobject" }
      }
      if client then
        client:send_error(node["bound-id"], -2, "no node available")
      end
    end
  else
    createLink (si, si_target, can_passthrough, exclusive)
    si_flags[si.id].was_handled = true
  end
end

function unhandleLinkable (si)
  local valid, si_props = checkLinkable(si)
  if not valid then
    return
  end

  Log.info (si, string.format("unhandling item: %s (%s)",
      tostring(si_props["node.name"]), tostring(si_props["node.id"])))

  -- remove any links associated with this item
  for silink in links_om:iterate() do
    local out_id = tonumber (silink.properties["out.item.id"])
    local in_id = tonumber (silink.properties["in.item.id"])
    if out_id == si.id or in_id == si.id then
      if out_id == si.id and
          si_flags[in_id] and si_flags[in_id].peer_id == out_id then
        si_flags[in_id].peer_id = nil
      elseif in_id == si.id and
          si_flags[out_id] and si_flags[in_id].peer_id == in_id then
        si_flags[out_id].peer_id = nil
      end
      silink:remove ()
      Log.info (silink, "... link removed")
    end
  end

  si_flags[si.id] = nil
end

function rescan()
  for si in linkables_om:iterate() do
    handleLinkable (si)
  end

  -- if pending_rescan, re-evaluate after sync
  if pending_rescan then
    pending_rescan = false
    Core.sync (function (c)
      rescan()
    end)
  end
end

default_nodes = Plugin.find("default-nodes-api")

metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
}

endpoints_om = ObjectManager { Interest { type = "SiEndpoint" } }

clients_om = ObjectManager { Interest { type = "client" } }

linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    -- only handle si-audio-adapter and si-node
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
  }
}

links_om = ObjectManager {
  Interest {
    type = "SiLink",
    -- only handle links created by this policy
    Constraint { "is.policy.item.link", "=", true },
  }
}

function cleanupTargetNodeMetadata()
  local metadata = metadata_om:lookup()
  if metadata then
    local to_remove = {}
    for s, k, t, v in metadata:iterate(Id.ANY) do
      if k == "target.node" then
        if v == "-1" then
          -- target.node == -1 is useless, it means the default node
          table.insert(to_remove, s)
        else
          -- if the target.node value is the same as the default node
          -- that would be selected for this stream, remove it
          local si = linkables_om:lookup { Constraint { "node.id", "=", s } }
          local properties = si.properties
          local def_id = getDefaultNode(properties, getTargetDirection(properties))
          if tostring(def_id) == v then
            table.insert(to_remove, s)
          end
        end
      end
    end

    for _, s in ipairs(to_remove) do
      metadata:set(s, "target.node", nil, nil)
    end
  end
end

-- listen for default node changes if config.follow is enabled
if config.follow then
  default_nodes:connect("changed", function ()
    cleanupTargetNodeMetadata()
    rescan()
  end)
end

-- listen for target.node metadata changes if config.move is enabled
if config.move then
  metadata_om:connect("object-added", function (om, metadata)
    metadata:connect("changed", function (m, subject, key, t, value)
      if key == "target.node" then
        rescan()
      end
    end)
  end)
end

linkables_om:connect("objects-changed", function (om)
  rescan()
end)

linkables_om:connect("object-removed", function (om, si)
  unhandleLinkable (si)
end)

metadata_om:activate()
endpoints_om:activate()
clients_om:activate()
linkables_om:activate()
links_om:activate()
