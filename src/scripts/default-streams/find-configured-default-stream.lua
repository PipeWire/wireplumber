-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- hook to make sure the user preferred stream(default.configured.*) in other
-- words currently selected stream is given higher priority

-- state-default-streams.lua also does find out the default stream out of the
-- user preferences(current and past), however it doesn't give any higher
-- priority to the currently selected stream.

log = Log.open_topic ("s-default-streams")

nutils = require ("node-utils")

SimpleEventHook {
  name = "default-streams/find-configured-default-stream",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-stream" },
    },
  },
  execute = function (event)
    local available_streams = event:get_data ("available-streams")
    local selected_prio = event:get_data ("selected-stream-priority") or 0
    local source = event:get_source ()
    local props = event:get_properties ()

    local available_stream_ids = available_streams and available_streams:parse ()
    if not available_stream_ids then
      return
    end

    -- Get the default metadata
    local metadata_om = source:call ("get-object-manager", "metadata")
    local metadata = metadata_om:lookup { Constraint { "metadata.name", "=", "default" } }
    if not metadata then
      return
    end

    -- Get the default configured value
    local def_stream_type = props ["default-stream.type"]
    local json_val = metadata:find (0, "default.configured." .. def_stream_type)
    if not json_val then
      return
    end
    local configured_val = Json.Raw (json_val):parse ()
    if not configured_val then
      return
    end

    local streams_om = source:call ("get-object-manager", "stream")
    for _, id in ipairs (available_stream_ids) do
      local stream = streams_om:lookup {
        type = "Stream",
        Constraint { "id", "=", id, type = "gobject" },
      }
      if stream == nil then
        goto skip_stream
      end

      -- If stream value is not defined, we consider the stream as matched
      if stream:get_property ("node.name") == configured_val.name and
          (configured_val.stream == nil or
              stream:get_port_group () == configured_val.stream) then
        local stream_props = stream:get_properties ()
        local priority = 30000 + nutils.get_session_priority (stream_props)

        if priority > selected_prio then
          event:set_data ("selected-stream-priority", priority)
          event:set_data ("selected-stream", stream)
        end

        break
      end

      ::skip_stream::
    end
  end
}:register ()
