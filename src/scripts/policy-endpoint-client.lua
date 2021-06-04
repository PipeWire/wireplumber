-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- Receive script arguments from config.lua
local config = ...
config.roles = config.roles or {}

function findRole(role)
  if role and not config.roles[role] then
    for r, p in pairs(config.roles) do
      if type(p.alias) == "table" then
        for i = 1, #(p.alias), 1 do
          if role == p.alias[i] then
            return r
          end
        end
      end
    end
  end
  return role
end

function getSessionItemById (si_id, om)
  return om:lookup {
    Constraint { "id", "=", tonumber(si_id), type = "gobject" }
  }
end

function findTargetEndpoint (node, media_class)
  local target_class_assoc = {
    ["Stream/Input/Audio"] = "Audio/Source",
    ["Stream/Output/Audio"] = "Audio/Sink",
    ["Stream/Input/Video"] = "Video/Source",
  }
  local media_role = nil
  local highest_priority = -1
  local target = nil

  -- get target media class
  local target_media_class = target_class_assoc[media_class]
  if not target_media_class then
    return nil
  end

  -- find highest priority endpoint by role
  media_role = findRole(node.properties["media.role"])
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
    if string.find (target_media_class, "Input") or
        string.find (target_media_class, "Sink") then
      out_context = "reverse"
    end
  else
    -- playback
    out_item = si
    in_item = si_target_ep
    if string.find (target_media_class, "Output") or
        string.find (target_media_class, "Source") then
      in_context = "reverse"
    end
  end

  Log.info (string.format("link %s <-> %s",
      tostring(node.properties["node.name"]),
      tostring(si_target_ep.properties["name"])))

  -- create and configure link
  local si_link = SessionItem ( "si-standard-link" )
  if not si_link:configure {
    ["out.item"] = out_item,
    ["in.item"] = in_item,
    ["out.item.port.context"] = out_context,
    ["in.item.port.context"] = in_context,
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
  if siendpoints_om:get_n_objects () == 0 or media_role == nil then
    Log.info (si, "item won't be handled by this policy")
    return false
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
  local media_role = node.properties["media.role"] or ""
  Log.info (si, "handling item " .. tostring(node.properties["node.name"]) ..
      " with role " .. media_role)

  -- find proper target endpoint
  local si_target_ep = findTargetEndpoint (node, media_class)
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

siendpoints_om = ObjectManager { Interest { type = "SiEndpoint" }}
silinkables_om = ObjectManager { Interest { type = "SiLinkable",
  -- only handle si-audio-adapter and si-node
  Constraint {
    "si.factory.name", "c", "si-audio-adapter", "si-node", type = "pw-global" },
  }
}
silinks_om = ObjectManager { Interest { type = "SiLink",
  -- only handle links created by this policy
  Constraint { "is.policy.endpoint.client.link", "=", true, type = "pw-global" },
} }

silinkables_om:connect("object-added", function (om, si)
  handleSiLinkable (si)
end)

silinkables_om:connect("object-removed", function (om, si)
  unhandleSiLinkable (si)
  reevaluateSiLinkables ()
end)

siendpoints_om:activate()
silinkables_om:activate()
silinks_om:activate()
