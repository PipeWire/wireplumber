-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target stream is a filter target.

stream_utils = require ("stream-utils")
futils = require ("filter-utils")

log = Log.open_topic ("s-linking")

function find_filter_target (stream)
  -- Return nil if stream is not a smart filter stream
  local direction = stream_utils.get_target_direction (stream)
  if not futils.is_filter_smart (direction, stream) then
    return nil, false
  end

  -- Get the filter target
  return futils.get_filter_target (direction, stream), true
end

SimpleEventHook {
  name = "linking/find-filter-target",
  after = "linking/find-audio-group-target",
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
    local stream_om = source:call ("get-object-manager", "stream")

    -- Bypass the hook if the target is already picked up
    local target = event:get_data ("target")
    if target ~= nil then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    local dont_fallback = stream_props:get_boolean ("node.dont-fallback")
    local is_smart_filter = false
    target, is_smart_filter = find_filter_target (stream)

    if target ~= nil then
      -- Make sure the target can be linked
      if not stream_utils.can_link (stream, target, stream_om) then
        log:debug (stream, "... cannot link, ignoring filter target")
        target = nil
      end

      -- Make sure the target is compatible
      if target ~= nil then
        local compatible, enc_only = stream:is_compatible (target)
        if not compatible then
          log:debug (stream, "... not compatible, ignoring filter target")
          target = nil
        end
      end
    end

    if target ~= nil then
      local target_props = target:get_properties ()
      local target_id = target.id
      local target_node_id = target:get_node_bound_id ()
      log:info (stream, string.format (
          "found filter target stream:%d (node:%d): %s",
          target_id, target_node_id, tostring (target_props["node.name"])))
      event:set_data ("target", target)
    elseif is_smart_filter and dont_fallback then
      -- Send error to client and destroy stream node if linger is not set
      local linger = stream_props:get_boolean ("node.linger")
      if not linger then
        local message = "smart filter defined target not found"
        if stream_utils.send_client_error (stream, source, -2, message) then
          log:debug (stream, "sent client error with message: " .. message)
        end
        local node = stream:get_node ()
        node:request_destroy ()
        log:info (stream, "destroyed stream node as smart filter defined target was not found")
      else
        log:info (stream, "waiting for smart filter defined target as dont-fallback is set")
      end
      event:stop_processing ()
    else
      -- continue with other hooks
      log:info (stream, "could not find filter target stream")
    end
  end
}:register ()
