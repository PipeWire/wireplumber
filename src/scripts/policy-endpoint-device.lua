-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

target_class_assoc = {
  ["Audio/Source"] = "Audio/Source",
  ["Audio/Sink"] = "Audio/Sink",
  ["Video/Source"] = "Video/Source",
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

function findTargetByDefaultNode (target_media_class, om)
  local def_id = default_nodes:call("get-default-node", target_media_class)
  if def_id ~= Id.INVALID then
    for si_target in om:iterate() do
      local target_node = si_target:get_associated_proxy ("node")
      if target_node["bound-id"] == def_id then
        return si_target
      end
    end
  end
  return nil
end

function findTargetByFirstAvailable (target_media_class, om)
  for si_target in om:iterate() do
    local target_node = si_target:get_associated_proxy ("node")
    if target_node.properties["media.class"] == target_media_class then
      return si_target
    end
  end
  return nil
end

function findUndefinedTarget (target_media_class, om)
  local si_target = findTargetByDefaultNode (target_media_class, om)
  if not si_target then
    si_target = findTargetByFirstAvailable (target_media_class, om)
  end
  return si_target
end

function createLink (si_ep, si_target)
  local target_node = si_target:get_associated_proxy ("node")
  local target_media_class = target_node.properties["media.class"]
  local out_item = nil
  local out_context = nil
  local in_item = nil
  local in_context = nil

  if string.find (target_media_class, "Input") or
      string.find (target_media_class, "Sink") then
    -- capture
    in_item = si_target
    out_item = si_ep
    out_context = "reverse"
  else
    -- playback
    in_item = si_ep
    out_item = si_target
    in_context = "reverse"
  end

  Log.info (string.format("link %s <-> %s",
      si_ep.properties["name"],
      target_node.properties["node.name"]))

  -- create and configure link
  local si_link = SessionItem ( "si-standard-link" )
  if not si_link:configure {
    ["out.item"] = out_item,
    ["in.item"] = in_item,
    ["out.item.port.context"] = out_context,
    ["in.item.port.context"] = in_context,
    ["manage.lifetime"] = false,
    ["passive"] = true,
    ["is.policy.endpoint.device.link"] = true,
  } then
    Log.warning (si_link, "failed to configure si-standard-link")
    return
  end

  -- register
  si_link:register ()

  -- activate
  si_link:activate (Feature.SessionItem.ACTIVE)
end

function getSiLinkAndSiPeer (si_ep, target_media_class)
  for silink in silinks_om:iterate() do
    local out_id = tonumber(silink.properties["out.item.id"])
    local in_id = tonumber(silink.properties["in.item.id"])
    if out_id == si_ep.id or in_id == si_ep.id then
      local is_out = out_id == si_ep.id and true or false
      for peer in siportinfos_om:iterate() do
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

function handleSiEndpoint (si_ep)
  -- only handle endpoints that have a valid target media class
  local media_class = si_ep.properties["media.class"]
  local target_media_class = target_class_assoc[media_class]
  if not target_media_class then
    return
  end

  Log.info (si_ep, "handling endpoint " .. si_ep.properties["name"])

  -- find proper target item
  local si_target = findUndefinedTarget (target_media_class, siportinfos_om)
  if not si_target then
    Log.info (si_ep, "target item not found")
    return
  end

  -- Check if item is linked to proper target endpoint, otherwise re-link
  local si_link, si_peer = getSiLinkAndSiPeer (si_ep, target_media_class)
  if si_link then
    if si_peer and si_peer.id == si_target.id then
      Log.info (si_ep, "already linked to proper target")
      return
    end

    si_link:remove ()
    Log.info (si_ep, "moving to new target")
  end

  -- create new link
  createLink (si_ep, si_target)
end

function reevaluateLinks ()
  -- check endpoints and register new links
  for si_ep in siendpoints_om:iterate() do
    handleSiEndpoint (si_ep)
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

default_nodes = Plugin.find("default-nodes-api")
siendpoints_om = ObjectManager { Interest { type = "SiEndpoint" }}
siportinfos_om = ObjectManager { Interest { type = "SiPortInfo",
  -- only handle si-audio-adapter and si-node
  Constraint {
    "si.factory.name", "c", "si-audio-adapter", "si-node", type = "pw-global" },
  }
}
silinks_om = ObjectManager { Interest { type = "SiLink",
  -- only handle links created by this policy
  Constraint { "is.policy.endpoint.device.link", "=", true, type = "pw-global" },
} }

-- listen for default node changes if config.follow is enabled
if config.follow then
  default_nodes:connect("changed", function (p)
    reevaluateLinks ()
  end)
end

siportinfos_om:connect("objects-changed", function (om)
  reevaluateLinks ()
end)

siendpoints_om:activate()
siportinfos_om:activate()
silinks_om:activate()
