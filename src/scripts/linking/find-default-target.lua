-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the default stream can be picked up as target, otherwise the first
-- usable stream of the default stream node.

stream_utils = require ("stream-utils")

log = Log.open_topic ("s-linking")

default_streams = Plugin.find ("default-streams-api")

function can_use_target (stream, target, stream_om)
  -- Make sure the target stream can be linked
  if not stream_utils.can_link (stream, target, stream_om) then
    log:debug (stream, "... cannot link, ignoring default target")
    return false
  end

  -- Make sure target stream is compatible
  local compatible, enc_only = stream:is_compatible (target)
  if not compatible then
    log:debug (stream, "... not compatible, ignoring default target")
    return false
  end

  return true
end

function find_default_target (source, stream, target_mc)
  local streams_om = source:call ("get-object-manager", "stream")
  local default_stream =
      default_streams:call ("get-default-stream", target_mc)
  if default_stream == nil then
    log:debug (stream, "no default stream for " .. target_mc)
    return nil
  end

  local default_node_id = default_stream:get_node_bound_id ()
  log:debug (stream, string.format (
      "default stream: media_class:%s stream:%d (node:%d)",
      target_mc, default_stream.id, default_node_id))

  if can_use_target (stream, default_stream, streams_om) then
    return default_stream
  end

  -- Otherwise use the first usable stream of the default stream node
  for target_stream in streams_om:iterate { type = "Stream" } do
    if target_stream ~= default_stream and
        target_stream:get_node_bound_id () == default_node_id and
        can_use_target (stream, target_stream, streams_om) then
      return target_stream
    end
  end

  return nil
end

SimpleEventHook {
  name = "linking/find-default-target",
  after = { "linking/find-defined-target",
            "linking/find-audio-group-target",
            "linking/find-filter-target",
            "linking/find-media-role-target",
            "linking/find-media-role-sink-target" },
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
    local target_mc = nil

    -- Bypass the hook if the target is already picked up
    local target = event:get_data ("target")
    if target ~= nil then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    if default_streams ~= nil then
      -- Get the target node media class for this stream
      target_mc = stream_utils.get_target_media_class (stream)
      if target_mc ~= nil then
        target = find_default_target (source, stream, target_mc)
      end
    else
      log:debug (stream, "default streams API module not found")
    end

    if target ~= nil then
      -- set event target and continue
      local target_props = target:get_properties()
      local target_id = target.id
      local target_node_id = target:get_node_bound_id ()
      log:info (stream, string.format (
          "found default target stream:%d (node:%d): %s",
          target_id, target_node_id, tostring (target_props["node.name"])))
      event:set_data ("target", target)
    else
      -- continue with other hooks
      log:info (stream, "could not find default target stream")
    end
  end
}:register ()
