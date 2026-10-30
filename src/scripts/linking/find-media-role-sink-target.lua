-- WirePlumber
--
-- Copyright © 2025 Phosh.mobi e.V.
--
-- SPDX-License-Identifier: MIT
--
-- Pick up a preferred target node for the output stream of role-based loopbacks

stream_utils = require ("stream-utils")

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-media-role-sink-target",
  after = { "linking/find-defined-target",
            "linking/find-media-role-target" },
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

    -- Bypass the hook if the target is already set or it is not a filter stream
    local target = event:get_data ("target")
    local media_class = stream_props["media.class"]
    local is_virtual = stream_props:get_boolean ("node.virtual")
    local streams_om = source:call ("get-object-manager", "stream")
    if target ~= nil or media_class ~= "Stream/Output/Audio" or not is_virtual or
        not stream_utils.is_filter_stream (stream, streams_om) then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    -- We link the output stream but the relevant properties are on the input
    -- stream of the filter
    local input_stream = stream_utils.get_filter_peer (stream, streams_om)
    if input_stream == nil then
      log:warning (stream, string.format ("no input stream for %s found",
          tostring (stream_props["node.name"])))
      return
    end

    -- Get the preferred target name if any
    local input_stream_props = input_stream:get_properties ()
    local target_name = input_stream_props["policy.role-based.preferred-target"]
    if target_name == nil then
      return
    end

    -- Find the target stream
    local target_stream = streams_om:lookup {
      type = "Stream",
      Constraint { "node.name", "=", target_name, type = "wp" },
    }
    if target_stream == nil then
      target_stream = streams_om:lookup {
        type = "Stream",
        Constraint { "node.nick", "=", target_name, type = "wp" },
      }
    end

    if target_stream and stream_utils.can_link (stream, target_stream, streams_om) then
      local target_props = target_stream:get_properties ()
      local target_id = target_stream.id
      local target_node_id = target_stream:get_node_bound_id ()
      log:info (stream, string.format (
          "role based sink target picked stream:%d (node:%d): %s",
          target_id, target_node_id, tostring (target_props["node.name"])))
      event:set_data ("target", target_stream)
    end
  end
}:register ()
