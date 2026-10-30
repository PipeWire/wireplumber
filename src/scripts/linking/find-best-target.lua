-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Traverse through all the possible targets to pick up the best one.

stream_utils = require ("stream-utils")
futils = require ("filter-utils")

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-best-target",
  after = { "linking/find-defined-target",
            "linking/find-audio-group-target",
            "linking/find-media-role-target",
            "linking/find-media-role-sink-target",
            "linking/find-default-target" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream = event:get_subject ()
    local stream_props = stream:get_properties ()
    local stream_id = stream.id
    local node_id = stream:get_node_bound_id ()
    local target_priority = 0
    local target_creation_time = 0

    -- Bypass the hook if the target is already picked up
    local target = event:get_data ("target")
    if target ~= nil then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    -- Find the best target matching the stream's target shape
    local streams_om = source:call ("get-object-manager", "stream")
    local devices_om = source:call ("get-object-manager", "device")
    local target_shape = stream_utils.get_target_shape (stream)
    local target_direction = stream_utils.get_target_direction (stream)
    for target_stream in streams_om:iterate {
      type = "Stream",
      Constraint { "shape", "=", target_shape, type = "gobject" },
    } do
      local target_id = target_stream.id
      local target_props = target_stream:get_properties ()
      local target_node_id = target_stream:get_node_bound_id ()

      -- Skip smart filters as best target
      if futils.is_filter_smart (target_direction, target_stream) then
        log:debug (stream, "... ignoring smart filter as best target")
        goto skip_target
      end

      local priority = target_props:get_int ("priority.session") or 0
      local creation_time = target_stream:get_creation_time()

      log:debug (stream, string.format (
          "checking target stream:%d (node:%d prio:%d creation:%d): %s",
          target_id, target_node_id, priority, creation_time,
          tostring (target_props ["node.name"])))

      -- Make sure the target can be linked
      if not stream_utils.can_link (stream, target_stream, streams_om) then
        log:debug (stream, "... cannot link, ignoring target")
        goto skip_target
      end

      -- Make sure target is compatible
      local compatible, enc_only = stream:is_compatible (target_stream)
      if not compatible then
        log:debug (stream, "... not compatible, ignoring target")
        goto skip_target
      end

      -- Make sure the target has available routes
      if not stream_utils.has_available_routes (target_stream, devices_om) then
        log:debug (stream, "... does not have available routes, ignoring target")
        goto skip_target
      end

      -- Select the target with highest priority and creation time
      if (target == nil or
          priority > target_priority or
          (priority == target_priority and
              creation_time > target_creation_time)) then
        log:debug (stream, "... picked")
        target = target_stream
        target_priority = priority
        target_creation_time = creation_time
      end

      ::skip_target::
    end

    if target ~= nil then
      -- set event target and continue
      local target_props = target:get_properties()
      local target_id = target.id
      local target_node_id = target:get_node_bound_id ()
      log:info (stream, string.format (
          "found best target stream:%d (node:%d): %s",
          target_id, target_node_id, tostring (target_props["node.name"])))
      event:set_data ("target", target)
    else
      -- continue with other hooks
      log:info (stream, "could not find best target stream")
    end
  end
}:register ()
