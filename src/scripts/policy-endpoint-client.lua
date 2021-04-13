-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

target_class_assoc = {
  ["Stream/Input/Audio"] = "Audio/Source",
  ["Stream/Output/Audio"] = "Audio/Sink",
  ["Stream/Input/Video"] = "Video/Source",
}

-- Receive script arguments from config.lua
local config = ...

-- ensure config.move and config.follow are not nil
config.move = config.move or false
config.follow = config.follow or false

function getSessionItemById (si_id, om)
  return om:lookup {
    Constraint { "id", "=", tonumber(si_id), type = "gobject" }
  }
end

function findTargetByTargetNodeMetadata (node, om)
  local node_id = node['bound-id']
  local metadata = metadatas_om:lookup()
  if metadata then
    local value = metadata:find(node_id, "target.node")
    if value then
      for si_target in om:iterate() do
        local target_node = si_target:get_associated_proxy ("node")
        if target_node["bound-id"] == tonumber(value) then
          return si_target
        end
      end
    end
  end
  return nil
end

function findTargetByNodeTargetProperty (node, om)
  local target_id_str = node.properties["node.target"]
  if target_id_str then
    for si_target in om:iterate() do
      local target_node = si_target:get_associated_proxy ("node")
      if target_node["bound-id"] == tonumber(target_id_str) then
        return si_target
      end
    end
  end
  return nil
end

function findDefinedTarget (node, om)
  local si_target = findTargetByTargetNodeMetadata (node, om)
  if not si_target then
    si_target = findTargetByNodeTargetProperty (node, om)
  end
  return si_target
end

function findTargetEndpoint (node, target_media_class)
  local media_role = node.properties["media.role"]
  local highest_priority = -1
  local target = nil

  -- find defined target
  target = findDefinedTarget (node, siendpoints_om)

  -- find highest priority endpoint by role
  if not target then
    for si_target_ep in siendpoints_om:iterate {
      Constraint { "role", "=", media_role, type = "pw-global" },
      Constraint { "media.class", "=", target_media_class, type = "pw-global" },
    } do
      local priority = tonumber(si_target_ep.properties["priority"])
      if priority > highest_priority then
        highest_priority = priority
        target = si_target_ep
      end
    end
  end

  -- find highest priority endpoint regardless of role
  if not target then
    for si_target_ep in siendpoints_om:iterate {
      Constraint { "media.class", "=", target_media_class, type = "pw-global" },
    } do
      local priority = tonumber(si_target_ep.properties["priority"])
      if priority > highest_priority then
        highest_priority = priority
        target = si_target_ep
      end
    end
  end

  return target
end

function createLink (si, si_target_ep)
  local node = si:get_associated_proxy ("node")
  local media_class = node.properties["media.class"]
  local target_media_class = si_target_ep.properties["media.class"]
  local out_item = nil
  local out_context = nil
  local in_item = nil
  local in_context = nil

  if string.find (media_class, "Input") or
      string.find (media_class, "Sink") then
    -- capture
    out_item = si_target_ep
    in_item = si
    if target_class_assoc[media_class] ~= target_media_class then
      out_context = "reverse"
    end
  else
    -- playback
    out_item = si
    in_item = si_target_ep
    if target_class_assoc[media_class] ~= target_media_class then
      in_context = "reverse"
    end
  end

  Log.info (string.format("link %s <-> %s",
      node.properties["node.name"],
      si_target_ep.properties["name"]))

  -- create and configure link
  local si_link = SessionItem ( "si-standard-link" )
  if not si_link:configure {
    ["out.item"] = out_item,
    ["in.item"] = in_item,
    ["out.item.port.context"] = out_context,
    ["in.item.port.context"] = in_context,
    ["manage.lifetime"] = false,
    ["is.policy.endpoint.client.link"] = true,
    ["media.role"] = si_target_ep.properties["role"],
    ["target.media.class"] = target_media_class,
    ["item.plugged.usec"] = si.properties["item.plugged.usec"],
  } then
    Log.warning (si_link, "failed to configure si-standard-link")
    return
  end

  -- register
  si_link:register()
end

function getSiLinkAndSiPeerEndpoint (si)
  for silink in silinks_om:iterate() do
    local out_id = tonumber(silink.properties["out.item.id"])
    local in_id = tonumber(silink.properties["in.item.id"])
    if out_id == si.id then
      return silink, getSessionItemById (in_id, siendpoints_om)
    elseif in_id == si.id then
      return silink, getSessionItemById (out_id, siendpoints_om)
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

  -- Determine if we can handle item by this policy
  local media_role = node.properties["media.role"]
  if siendpoints_om:get_n_objects () == 0 or media_role == nil then
    Log.info (si, "item won't be handled by this policy")
    return
  end

  Log.info (si, "handling item " .. node.properties["node.name"] ..
      " with role " .. media_role)

  -- find proper target endpoint
  local si_target_ep = findTargetEndpoint (node, target_media_class)
  if not si_target_ep then
    Log.info (si, "target endpoint not found")
    return
  end

  -- Check if item is linked to proper target endpoint, otherwise re-link
  local si_link, si_peer_ep = getSiLinkAndSiPeerEndpoint (si)
  if si_link then
    if si_peer_ep and si_peer_ep.id == si_target_ep.id then
      Log.info (si, "already linked to proper target endpoint")
      return
    end

    si_link:remove ()
    Log.info (si, "moving to new target endpoint")
  end

  -- create new link
  createLink (si, si_target_ep)
end

function reevaluateLinks ()
  -- check port info session items and register new links
  for si in siportinfos_om:iterate() do
    handleSiPortInfo (si)
  end

  -- check link session items and unregister them if not used
  for silink in silinks_om:iterate() do
    local out_id = tonumber (silink.properties["out.item.id"])
    local in_id = tonumber (silink.properties["in.item.id"])
    if (getSessionItemById (out_id, siendpoints_om) and not getSessionItemById (in_id, siportinfos_om)) or
        (getSessionItemById (in_id, siendpoints_om) and not getSessionItemById (out_id, siportinfos_om)) then
      silink:remove ()
      Log.info (silink, "link removed")
    end
  end
end

default_nodes = Plugin("default-nodes-api")
metadatas_om = ObjectManager { Interest { type = "metadata" } }
siendpoints_om = ObjectManager { Interest { type = "SiEndpoint" }}
siportinfos_om = ObjectManager { Interest { type = "SiPortInfo",
  -- only handle si-audio-adapter and si-node
  Constraint {
    "si.factory.name", "c", "si-audio-adapter", "si-node", type = "pw-global" },
  }
}
silinks_om = ObjectManager { Interest { type = "SiLink",
  -- only handle links created by this policy
  Constraint { "is.policy.endpoint.client.link", "=", true, type = "pw-global" },
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
siendpoints_om:activate()
siportinfos_om:activate()
silinks_om:activate()
