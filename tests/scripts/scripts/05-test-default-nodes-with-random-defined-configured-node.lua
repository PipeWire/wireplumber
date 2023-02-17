-- default nodes scripts should give priority to the current preferred device
-- over and above the perviously preferend devices and available devices.

-- create a device and select it as prefered device(DCN keys) the last prefered
-- device should be picked up as default node, repeat this for an arbitrary
-- no. of devices and then select a random device as preferred device, this
-- device should be picked up as default device.

local tu = require("test-utils")
local config = require("device-config")

Script.async_activation = true

local default_nodes_stack_depth = 16

local d_iterator = 0

function createDeviceNodes ()
  tu.createDeviceNode ("audio-sink-device-node-" .. d_iterator, "Audio/Sink")
  tu.createDeviceNode ("audio-source-device-node-" .. d_iterator, "Audio/Source")
  tu.createDeviceNode ("video-source-device-node-" .. d_iterator, "Video/Source")
end

createDeviceNodes ()

function destroyDeviceNodes ()
  tu.destroyDeviceNode ("audio-sink-device-node-" .. d_iterator)
  tu.destroyDeviceNode ("audio-source-device-node-" .. d_iterator)
  tu.destroyDeviceNode ("video-source-device-node-" .. d_iterator)
end

local device_tracker = {
  ["default.audio.sink"] = false,
  ["default.audio.source"] = false,
  ["default.video.source"] = false
}

function all_devices_reported ()
  for k, v in pairs(device_tracker) do
    if v == false then
      return false
    end
  end
  return true
end

function reset_all_devices_reported ()
  for k, v in pairs(device_tracker) do
    device_tracker [k] = false
  end
end

local dkeys_table = {
  ["default.audio.sink"] = "audio-sink-device-node",
  ["default.audio.source"] = "audio-source-device-node",
  ["default.video.source"] = "video-source-device-node",
}

local test_state = "creation"

function updateDCN (iter)
  tu.default_metadata:set(0, "default.configured.audio.sink", "Spa:String:JSON",
      Json.Object { ["name"] = "audio-sink-device-node-" .. iter }:to_string())
  tu.default_metadata:set(0, "default.configured.audio.source", "Spa:String:JSON",
      Json.Object { ["name"] = "audio-source-device-node-" .. iter }:to_string())
  tu.default_metadata:set(0, "default.configured.video.source", "Spa:String:JSON",
      Json.Object { ["name"] = "video-source-device-node-" .. iter }:to_string())
end

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
  execute = function (event)
    local source = event:get_source ()
    local props = event:get_properties ()
    local key = props ["event.subject.key"]
    local value_string = props ["event.subject.value"]
    local value_json = Json.Raw (value_string)
    local value = value_json:parse ().name

    local expected_value = dkeys_table [key] .. "-" .. d_iterator

    if value == expected_value then
      device_tracker [key] = true;
    end

    if all_devices_reported () then
      reset_all_devices_reported ()

      if test_state == "creation" then
        if d_iterator == default_nodes_stack_depth then
          d_iterator = 5
          updateDCN (d_iterator)
          test_state = "random-device"
        else
          d_iterator = d_iterator + 1
          createDeviceNodes ()
        end
      elseif test_state == "random-device" then
        tu.clearDefaultNodeState ()
        Script:finish_activation ()
      end
    end
  end
}:register()

SimpleEventHook {
  name = "linkable-added@test-default-nodes",
  after = "linkable-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "session-item-added" },
      Constraint { "event.session-item.interface", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    },
  },
  execute = function (event)
    local lnkbl = event:get_subject ()
    local name = lnkbl.properties ["node.name"]
    local mc = lnkbl.properties ["media.class"]

    tu.default_metadata:set(0, tu.dcn_keys [mc], "Spa:String:JSON",
        Json.Object { ["name"] = name }:to_string ())
  end
}:register()
