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

function createLink (si, si_target)
  local node = si:get_associated_proxy ("node")
  local target_node = si_target:get_associated_proxy ("node")
  local media_class = node.properties["media.class"]
  local target_media_class = target_node.properties["media.class"]
  local out_item = nil
  local out_context = nil
  local in_item = nil
  local in_context = nil

  if string.find (media_class, "Input") or
      string.find (media_class, "Sink") then
    -- capture
    out_item = si_target
    in_item = si
    if string.find (target_media_class, "Input") or
        string.find (target_media_class, "Sink") then
      out_context = "reverse"
    end
  else
    -- playback
    out_item = si
    in_item = si_target
    if string.find (target_media_class, "Output") or
        string.find (target_media_class, "Source") then
      in_context = "reverse"
    end
  end

  Log.info (string.format("link %s <-> %s",
      tostring(node.properties["node.name"]),
      tostring(target_node.properties["node.name"])))

  -- create and configure link
  local si_link = SessionItem ( "si-standard-link" )
  if not si_link:configure {
    ["out.item"] = out_item,
    ["in.item"] = in_item,
    ["out.item.port.context"] = out_context,
    ["in.item.port.context"] = in_context,
    ["is.policy.item.link"] = true,
  } then
    Log.warning (si_link, "failed to configure si-standard-link")
    return
  end

  -- register
  si_link:register ()

  -- activate
  si_link:activate (Feature.SessionItem.ACTIVE)
end

function findTargetByTargetNodeMetadata (node)
  local node_id = node['bound-id']
  local metadata = metadata_om:lookup()
  if metadata then
    local value = metadata:find(node_id, "target.node")
    if value then
      for si_target in silinkables_om:iterate() do
        local target_node = si_target:get_associated_proxy ("node")
        if target_node["bound-id"] == tonumber(value) then
          return si_target
        end
      end
    end
  end
  return nil
end

function findTargetByNodeTargetProperty (node)
  local target_id_str = node.properties["node.target"]
  if target_id_str then
    for si_target in silinkables_om:iterate() do
      local target_node = si_target:get_associated_proxy ("node")
      local target_props = target_node.properties
      if target_node["bound-id"] == tonumber(target_id_str) or
         target_props["node.name"] == target_id_str or
         target_props["object.path"] == target_id_str then
        return si_target
      end
    end
  end
  return nil
end

function findTargetByDefaultNode (target_media_class)
  local def_id = default_nodes:call("get-default-node", target_media_class)
  if def_id ~= Id.INVALID then
    for si_target in silinkables_om:iterate() do
      local target_node = si_target:get_associated_proxy ("node")
      if target_node["bound-id"] == def_id then
        return si_target
      end
    end
  end
  return nil
end

function findTargetByFirstAvailable (target_media_class)
  for si_target in silinkables_om:iterate() do
    local target_node = si_target:get_associated_proxy ("node")
    if target_node.properties["media.class"] == target_media_class then
      return si_target
    end
  end
  return nil
end

function findDefinedTarget (node)
  local si_target = findTargetByTargetNodeMetadata (node)
  if not si_target then
    si_target = findTargetByNodeTargetProperty (node)
  end
  return si_target
end

function findUndefinedTarget (media_class)
  local target_class_assoc = {
    ["Stream/Input/Audio"] = "Audio/Source",
    ["Stream/Output/Audio"] = "Audio/Sink",
    ["Stream/Input/Video"] = "Video/Source",
  }
  local si_target = nil

  local target_media_class = target_class_assoc[media_class]
  if target_media_class then
    si_target = findTargetByDefaultNode (target_media_class)
    if not si_target then
      si_target = findTargetByFirstAvailable (target_media_class)
    end
  end
  return si_target
end

function getSiLinkAndSiPeer (si, target_media_class)
  for silink in silinks_om:iterate() do
    local out_id = tonumber(silink.properties["out.item.id"])
    local in_id = tonumber(silink.properties["in.item.id"])
    if out_id == si.id or in_id == si.id then
      local is_out = out_id == si.id and true or false
      for peer in silinkables_om:iterate() do
        if peer.id == (is_out and in_id or out_id) then
          local peer_node = peer:get_associated_proxy ("node")
          local peer_media_class = peer_node.properties["media.class"]
          if peer_media_class == target_media_class then
            return silink, peer
          end
        end
      end
    end
  end
  return nil, nil
end

function isSiLinkableValid (si)
  -- only handle session items that has a node associated proxy
  local node = si:get_associated_proxy ("node")
  if not node or not node.properties then
    return false
  end

  -- only handle stream session items
  local media_class = node.properties["media.class"]
  if not media_class or not string.find (media_class, "Stream") then
    return false
  end

  -- Determine if we can handle item by this policy
  local media_role = node.properties["media.role"]
  if siendpoints_om:get_n_objects () > 0 and media_role ~= nil then
    return false
  end

  return true
end

function getNodeAutoconnect (node)
  local auto_connect = node.properties["node.autoconnect"]
  if auto_connect then
    return (auto_connect == "true" or auto_connect == "1")
  end
  return false
end

function getNodeReconnect (node)
  local dont_reconnect = node.properties["node.dont-reconnect"]
  if dont_reconnect then
    return not (dont_reconnect == "true" or dont_reconnect == "1")
  end
  return true
end

function handleSiLinkable (si)
  -- check if item is valid
  if not isSiLinkableValid (si) then
    return
  end

  local node = si:get_associated_proxy ("node")
  local media_class = node.properties["media.class"] or ""
  Log.info (si, "handling item " .. tostring(node.properties["node.name"]))

  local autoconnect = getNodeAutoconnect (node)
  if not autoconnect then
    Log.info (si, "node does not need to be autoconnected")
    return
  end

  -- get reconnect
  local reconnect = getNodeReconnect (node)

  -- find target
  local si_target = findDefinedTarget (node)
  if not si_target and not reconnect then
    Log.info (si, "removing item and node")
    si:remove()
    node:request_destroy()
    return
  elseif not si_target and reconnect then
    si_target = findUndefinedTarget (media_class)
  end
  if not si_target then
    Log.info (si, "target not found")
    return
  end

  -- Check if item is linked to proper target, otherwise re-link
  local target_node = si_target:get_associated_proxy ("node")
  local target_media_class = target_node.properties["media.class"] or ""
  local si_link, si_peer = getSiLinkAndSiPeer (si, target_media_class)
  if si_link then
    if si_peer and si_peer.id == si_target.id then
      Log.debug (si, "already linked to proper target")
      return
    end

    si_link:remove ()
    Log.info (si, "moving to new target")
  end

  -- create new link
  createLink (si, si_target)
end

function unhandleSiLinkable (si)
  -- check if item is valid
  if not isSiLinkableValid (si) then
    return
  end

  local node = si:get_associated_proxy ("node")
  Log.info (si, "unhandling item " .. tostring(node.properties["node.name"]))

  -- remove any links associated with this item
  for silink in silinks_om:iterate() do
    local out_id = tonumber (silink.properties["out.item.id"])
    local in_id = tonumber (silink.properties["in.item.id"])
    if out_id == si.id or in_id == si.id then
      silink:remove ()
      Log.info (silink, "link removed")
    end
  end
end

function reevaluateSiLinkables ()
  for si in silinkables_om:iterate() do
    handleSiLinkable (si)
  end
end

default_nodes = Plugin.find("default-nodes-api")
metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
}
siendpoints_om = ObjectManager { Interest { type = "SiEndpoint" }}
silinkables_om = ObjectManager { Interest { type = "SiLinkable",
  -- only handle si-audio-adapter and si-node
  Constraint {
    "si.factory.name", "c", "si-audio-adapter", "si-node", type = "pw-global" },
  }
}
silinks_om = ObjectManager { Interest { type = "SiLink",
  -- only handle links created by this policy
  Constraint { "is.policy.item.link", "=", true, type = "pw-global" },
} }

-- listen for default node changes if config.follow is enabled
if config.follow then
  default_nodes:connect("changed", function (p)
    reevaluateSiLinkables ()
  end)
end

-- listen for target.node metadata changes if config.move is enabled
if config.move then
  metadata_om:connect("object-added", function (om, metadata)
    metadata:connect("changed", function (m, subject, key, t, value)
      if key == "target.node" then
        reevaluateSiLinkables ()
      end
    end)
  end)
end

silinkables_om:connect("objects-changed", function (om, si)
  reevaluateSiLinkables ()
end)

silinkables_om:connect("object-removed", function (om, si)
  unhandleSiLinkable (si)
  reevaluateSiLinkables ()
end)

metadata_om:activate()
siendpoints_om:activate()
silinkables_om:activate()
silinks_om:activate()
