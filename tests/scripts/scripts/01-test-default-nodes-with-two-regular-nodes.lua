-- checks if the correct node is picked up as default node when two regular
-- nodes are created.

local tu = require("test-utils")

Script.async_activation = true

tu.createDeviceNode ("audio-sink-device-node", "Audio/Sink")
tu.createDeviceNode ("audio-source-device-node", "Audio/Source")
tu.createDeviceNode ("video-source-device-node", "Video/Source")

tu.createDeviceNode ("audio-sink-device-node-1", "Audio/Sink")
tu.createDeviceNode ("audio-source-device-node-1", "Audio/Source")
tu.createDeviceNode ("video-source-device-node-1", "Video/Source")

local expected_values_table = {
  ["default.audio.sink"] = "audio-sink-device-node-1",
  ["default.audio.source"] = "audio-source-device-node-1",
  ["default.video.source"] = "video-source-device-node-1",
}

local device_match_count = 0

SimpleEventHook {
  name = "test-default-nodes/metadata-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "c", "default.audio.source",
        "default.audio.sink", "default.video.source" },
    },
  },
  execute = function(event)
    local source = event:get_source ()
    local props = event:get_properties ()
    local key = props ["event.subject.key"]
    local value_string = props ["event.subject.value"]
    local value_json = Json.Raw (value_string)
    local value = value_json:parse ().name

    if expected_values_table [key] == value then
      device_match_count = device_match_count + 1
    end

    if device_match_count == 3 then
      tu.clearDefaultNodeState ()
      Script:finish_activation ()
    end
  end
}:register ()
