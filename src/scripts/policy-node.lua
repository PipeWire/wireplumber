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

target_class_assoc = {
  ["Stream/Input/Audio"] = "Audio/Source",
  ["Stream/Output/Audio"] = "Audio/Sink",
  ["Stream/Input/Video"] = "Video/Source",
}

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
    if target_class_assoc[media_class] ~= target_media_class then
      out_context = "reverse"
    end
  else
    -- playback
    out_item = si
    in_item = si_target
    if target_class_assoc[media_class] ~= target_media_class then
      in_context = "reverse"
    end
  end

  Log.info (string.format("link %s <-> %s",
      node.properties["node.name"],
      target_node.properties["node.name"]))

  -- create and configure link
  local si_link = SessionItem ( "si-standard-link" )
  if not si_link:configure {
    ["out.item"] = out_item,
    ["in.item"] = in_item,
    ["out.item.port.context"] = out_context,
    ["in.item.port.context"] = in_context,
    ["manage.lifetime"] = false,
    ["is.policy.item.link"] = true,
  } then
    Log.warning (si_link, "failed to configure si-standard-link")
  end

  -- activate and register
  si_link:activate (Feature.SessionItem.ACTIVE, function (link)
    Log.info (link, "link activated")
    link:register ()
  end)
end

function findTargetByTargetNodeMetadata (node)
  local node_id = node['bound-id']
  local metadata = metadatas_om:lookup()
  if metadata then
    local value = metadata:find(node_id, "target.node")
    if value then
      for si_target in siportinfos_om:iterate() do
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
    for si_target in siportinfos_om:iterate() do
      local target_node = si_target:get_associated_proxy ("node")
      if target_node["bound-id"] == tonumber(target_id_str) then
        return si_target
      end
    end
  end
  return nil
end

function findTargetByDefaultNode (target_media_class)
  local def_id = default_nodes:call("get-default-node", target_media_class)
  if def_id ~= Id.INVALID then
    for si_target in siportinfos_om:iterate() do
      local target_node = si_target:get_associated_proxy ("node")
      if target_node["bound-id"] == def_id then
        return si_target
      end
    end
  end
  return nil
end

function findTargetByFirstAvailable (target_media_class)
  for si_target in siportinfos_om:iterate() do
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

function findUndefinedTarget (target_media_class)
  local si_target = findTargetByDefaultNode (target_media_class)
  if not si_target then
    si_target = findTargetByFirstAvailable (target_media_class)
  end
  return si_target
end

function getSiLinkAndSiPeer (si)
  for silink in silinks_om:iterate() do
    local out_id = tonumber(silink.properties["out.item.id"])
    local in_id = tonumber(silink.properties["in.item.id"])
    if out_id == si.id then
      return silink, siportinfos_om:lookup {
        Constraint { "id", "=", in_id, type = "gobject" }
      }
    elseif in_id == si.id then
      return silink, siportinfos_om:lookup {
        Constraint { "id", "=", out_id, type = "gobject" }
      }
    end
  end
  return nil, nil
end

function handleSiPortInfo (si)
  -- only handle session items that has a node associated proxy
  local node = si:get_associated_proxy ("node")
  if not node or not node.properties then
    return
  end

  -- only handle session item that has a valid target media class
  local media_class = node.properties["media.class"]
  local target_media_class = target_class_assoc[media_class]
  if not target_media_class then
    return
  end

  Log.info (si, "handling item " .. node.properties["node.name"])

  -- find target
  local si_target = findDefinedTarget (node)
  if not si_target then
    si_target = findUndefinedTarget (target_media_class)
  end
  if not si_target then
    Log.info (si, "target not found")
    return
  end

  -- Check if item is linked to proper target, otherwise re-link
  local si_link, si_peer = getSiLinkAndSiPeer (si)
  if si_link then
    if si_peer and si_peer.id == si_target.id then
      Log.info (si, "already linked to proper target")
      return
    end

    si_link:remove ()
    Log.info (si, "moving to new target")
  end

  -- create new link
  createLink (si, si_target)
end

function reevaluateLinks ()
  -- check port info session items and register new links
  for si in siportinfos_om:iterate() do
    handleSiPortInfo (si)
  end

  -- check link session items and unregister them if not used
  for silink in silinks_om:iterate() do
    local used = 0
    local out_id_str = silink.properties["out.item.id"]
    local in_id_str = silink.properties["in.item.id"]
    for si in siportinfos_om:iterate() do
      if tonumber (out_id_str) == si.id or tonumber (in_id_str) == si.id then
        used = used + 1
      end
    end
    if used ~= 2 then
      silink:remove ()
      Log.info (silink, "link removed")
    end
  end
end

default_nodes = Plugin("default-nodes-api")
metadatas_om = ObjectManager { Interest { type = "metadata" } }
siportinfos_om = ObjectManager { Interest { type = "SiPortInfo",
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
    reevaluateLinks ()
  end)
end

-- listen for target.node metadata changes if config.move is enabled
if config.move then
  metadatas_om:connect("object-added", function (om, metadata)
    metadata:connect("changed", function (m, subject, key, t, value)
      if key == "target.node" then
        reevaluateLinks ()
      end
    end)
  end)
end

siportinfos_om:connect("objects-changed", function (om)
  reevaluateLinks ()
end)

metadatas_om:activate()
siportinfos_om:activate()
silinks_om:activate()
