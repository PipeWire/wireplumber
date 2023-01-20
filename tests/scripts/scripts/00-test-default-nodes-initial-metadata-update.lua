-- check if default keys are restored correctly by the default-nodes hooks on
-- bootup.

-- First we create a bunch of devices and update the default.configured.* keys
-- in other words select the default device.
-- then reload the metadata plugin which recreates the default metadata.
-- default-node/* scripts are supposed to restore the previously selected
-- default device.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("audio-sink-device-node", "Audio/Sink")
tu.createDeviceNode ("audio-source-device-node", "Audio/Source")
tu.createDeviceNode ("video-source-device-node", "Video/Source")

-- create second pair of devices just to make the test case better
tu.createDeviceNode ("audio-sink-device-node-1", "Audio/Sink")
tu.createDeviceNode ("audio-source-device-node-1", "Audio/Source")
tu.createDeviceNode ("video-source-device-node-1", "Video/Source")

local device_match_count = 0

local expected_values_table = {
  ["default.configured.audio.sink"] = "audio-sink-device-node",
  ["default.configured.audio.source"] = "audio-source-device-node",
  ["default.configured.video.source"] = "video-source-device-node",
  ["audio-sink-device-node"] = "default.configured.audio.sink",
  ["audio-source-device-node"] = "default.configured.audio.source",
  ["video-source-device-node"] = "default.configured.video.source",
}

SimpleEventHook {
  name = "node/test-default-nodes",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "node.name", "c", "audio-sink-device-node",
          "audio-source-device-node", "video-source-device-node" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local name = node.properties ["node.name"]
    local key = expected_values_table [name]

    device_match_count = device_match_count + 1

    tu.metadata:set (0, key, "Spa:String:JSON",
        Json.Object { ["name"] = name }:to_string ())

    if device_match_count == 3 then
      tu.restartPlugin ("metadata")
      device_match_count = 0
    end
  end
}:register ()

metadata_added_hook = SimpleEventHook {
  name = "test-default-nodes/metadata-added",
  after = "default-nodes/metadata-added",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-added" },
      Constraint { "metadata.name", "=", "default" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local om = source:call ("get-object-manager", "metadata")
    local metadata = om:lookup { Constraint { "metadata.name", "=", "default" } }

    for k, v in pairs (expected_values_table) do
      local obj = metadata:find (0, k)
      if obj then
        local json = Json.Raw (obj)
        if json:parse ().name == v then
          device_match_count = device_match_count + 1
        end
      end
    end

    if device_match_count == 3 then
      Script:finish_activation ()
    end

  end
}:register ()
