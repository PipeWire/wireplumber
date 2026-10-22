-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- looks for changes in user-preferences and devices added/removed and schedules
-- rescan and pushes "select-default-stream" event for each default stream type

cutils = require ("common-utils")
stream_utils = require ("stream-utils")

log = Log.open_topic ("s-default-streams")

-- looks for changes in user-preferences and devices added/removed and schedules
-- rescan
SimpleEventHook {
  name = "default-streams/rescan-trigger",
  interests = {
    EventInterest {
      Constraint { "event.type", "c", "stream-added", "stream-removed" },
      Constraint { "shape", "c", StreamShape.SINK, StreamShape.SOURCE,
          type = "gobject" },
    },
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "c", "default.configured.audio.sink",
          "default.configured.audio.source", "default.configured.video.source"
      },
    },
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed"},
      Constraint { "event.subject.param-id", "c", "Route", "EnumRoute"},
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("schedule-rescan", "default-streams")
  end
}:register ()

-- pushes "select-default-stream" event for each default stream type
SimpleEventHook {
  name = "default-streams/rescan",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "rescan-for-default-streams" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local streams_om = source:call ("get-object-manager", "stream")
    local devices_om = source:call ("get-object-manager", "device")

    log:trace ("re-evaluating default streams")

    -- Audio Sink
    pushSelectDefaultStreamEvent (source, streams_om, devices_om, "audio.sink",
        "AudioStream", { StreamShape.SINK })

    -- Audio Source, which can also be the monitor of a sink
    pushSelectDefaultStreamEvent (source, streams_om, devices_om,
        "audio.source", "AudioStream", { StreamShape.SOURCE, StreamShape.SINK })

    -- Video Source
    pushSelectDefaultStreamEvent (source, streams_om, devices_om,
        "video.source", "VideoStream", { StreamShape.SOURCE })
  end
}:register ()

function pushSelectDefaultStreamEvent (source, streams_om, devices_om,
    def_stream_type, stream_type, shapes)
  local ids = collectAvailableStreams (streams_om, devices_om, stream_type,
      shapes)
  local event = source:call ("create-event", "select-default-stream", nil, {
      ["default-stream.type"] = def_stream_type,
  })
  event:set_data ("available-streams", Json.Array (ids))
  EventDispatcher.push_event (event)
end

-- Return an array table with the ids of all the streams that can be selected
-- for a given stream type and set of shapes
function collectAvailableStreams (streams_om, devices_om, stream_type, shapes)
  local collected = {}

  for stream in streams_om:iterate { type = stream_type } do
    if not cutils.arrayContains (shapes, stream:get_shape ()) then
      goto next_stream
    end

    -- if the stream is associated to a real device, check that its node has
    -- available routes
    if not stream_utils.has_available_routes (stream, devices_om) then
      goto next_stream
    end

    table.insert (collected, stream.id)
    ::next_stream::
  end

  return collected
end
