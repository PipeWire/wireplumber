-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target stream is an audio group target.

stream_utils = require ("stream-utils")
agutils = require ("audio-group-utils")

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-audio-group-target",
  after = "linking/find-defined-target",
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

    -- Bypass the hook if the target is already picked up
    local target = event:get_data ("target")
    if target ~= nil then
      return
    end

    -- Bypass the hook if the stream is not part of an audio group
    local audio_group = agutils.get_audio_group (stream)
    if audio_group == nil then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    local target_shape = stream_utils.get_target_shape (stream)
    local streams_om = source:call ("get-object-manager", "stream")

    -- find the target with the same audio group, if any
    for target_stream in streams_om:iterate {
      type = "Stream",
      Constraint { "shape", "=", target_shape, type = "gobject" },
    } do
      local target_stream_props = target_stream:get_properties ()
      local target_audio_group = target_stream_props ["session.audio-group"]
      if target_audio_group == nil or target_audio_group ~= audio_group then
        goto skip_target
      end

      -- Make sure the target is compatible
      local compatible, enc_only = stream:is_compatible (target_stream)
      if not compatible then
        log:debug (stream, "... not compatible, ignoring target")
        goto skip_target
      end

      target = target_stream
      break

      ::skip_target::
    end

    if target ~= nil then
      local target_props = target:get_properties ()
      local target_id = target.id
      local target_node_id = target:get_node_bound_id ()
      log:info (stream, string.format (
          "found audio group target stream:%d (node:%d): %s",
          target_id, target_node_id, tostring (target_props["node.name"])))
      event:set_data ("target", target)
    end
  end
}:register ()
