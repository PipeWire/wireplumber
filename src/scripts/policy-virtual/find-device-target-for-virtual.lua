-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--

local putils = require ("policy-utils")

function findTargetByDefaultNode (target_media_class)
  local def_id = default_nodes:call("get-default-node", target_media_class)
  if def_id ~= Id.INVALID then
    for si_target in linkables_om:iterate() do
      local target_node = si_target:get_associated_proxy ("node")
      if target_node["bound-id"] == def_id then
        return si_target
      end
    end
  end
  return nil
end

function findTargetByFirstAvailable (target_media_class)
  for si_target in linkables_om:iterate() do
    local target_node = si_target:get_associated_proxy ("node")
    if target_node.properties["media.class"] == target_media_class then
      return si_target
    end
  end
  return nil
end

function findBestTarget (si)
  local media_class = si.properties["media.class"]
  local target_class_assoc = {
    ["Audio/Source"] = "Audio/Source",
    ["Audio/Sink"] = "Audio/Sink",
    ["Video/Source"] = "Video/Source",
  }
  local target_media_class = target_class_assoc[media_class]
  if not target_media_class then
    return nil
  end

  local target = findTargetByDefaultNode (target_media_class)
  if not target then
    target = findTargetByFirstAvailable (target_media_class)
  end
  return target
end

SimpleEventHook {
  name = "select-device-target@policy-virtual",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-si-device-and-link" },
    },
  },
  execute = function (event)
    local source, om, si, si_props, si_flags, target =
        putils:unwrap_find_target_event (event)

    -- bypass the hook if the target is already picked up
    if target then
      return
    end

    Log.info (si, string.format ("handling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    local target = findBestTarget (si)
    if target == nil then
      event:stop_processing ()
    else
      event:set_data ("target", target)
      source:call ("push-event", "find-target-si-and-link", si, nil)
    end
  end
}:register ()
