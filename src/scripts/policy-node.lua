-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

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
    ["out-item"] = out_item,
    ["in-item"] = in_item,
    ["out-item-port-context"] = out_context,
    ["in-item-port-context"] = in_context,
    ["manage-lifetime"] = false,
  } then
    Log.warning (si_link, "failed to configure si-standard-link")
  end

  -- activate and register
  si_link:activate (Feature.SessionItem.ACTIVE, function (link)
    Log.info (link, "link activated")
    link:register ()
  end)
end

function findTarget (node, target_media_class)
  local target = nil

  -- honor node.target, if present
  local target_id = node.properties["node.target"]
  if target_id then
    for candidate_si in siportinfos_om:iterate() do
      local n = candidate_si:get_associated_proxy ("node")
      if n and n['bound-id'] == tonumber(target_id) then
        target = candidate_si
        break
      end
    end
  end

  -- try to find the best target
  if target == nil then
    local def_id = default_nodes:call("get-default-node", target_media_class)

    for candidate_si in siportinfos_om:iterate() do
      local n = candidate_si:get_associated_proxy ("node")
      if n and n.properties["media.class"] == target_media_class then
        -- honor default node, if present
        local n_id = n["bound-id"]
        if def_id ~= Id.INVALID and n_id == def_id then
          target = candidate_si
          Log.debug (node, "choosing default node " .. n_id)
          break
        end

        -- otherwise just use this candidate
        if target == nil then
          target = candidate_si
        end
      end
    end
  end

  return target
end

function handleSiPortInfo (si)
  -- only handle unlinked session items
  for silink in silinks_om:iterate() do
    local out_id_str = silink.properties["out-item-id"]
    local in_id_str = silink.properties["in-item-id"]
    if tonumber (out_id_str) == si.id or tonumber (in_id_str) == si.id then
      return
    end
  end

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

  -- find a suitable target and link
  local si_target = findTarget (node, target_media_class)
  if si_target then
    createLink (si, si_target)
  end
end

function reevaluateSiPortInfos ()
  -- check port info session items and register new links
  for si in siportinfos_om:iterate() do
    handleSiPortInfo (si)
  end
end

function reevaulateSiLinks ()
  -- check link session items and unregister them if not used
  for silink in silinks_om:iterate() do
    local used = false
    for si in siportinfos_om:iterate() do
      local out_id_str = silink.properties["out-item-id"]
      local in_id_str = silink.properties["out-item-id"]
      if tonumber (out_id_str) == si.id or tonumber (in_id_str) == si.id then
        used = true
        break
      end
    end
    if not used then
      silink:remove ()
      Log.info (silink, "link removed")
    end
  end
end

default_nodes = Plugin("default-nodes-api")
siportinfos_om = ObjectManager { Interest { type = "SiPortInfo" } }
silinks_om = ObjectManager { Interest { type = "SiLink" } }

siportinfos_om:connect("objects-changed", function (om)
  reevaluateSiPortInfos ()
  reevaulateSiLinks ()
end)

siportinfos_om:activate()
silinks_om:activate()
