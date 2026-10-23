-- Tests that stream/configure-ports.lua configures the ports of a device stream
-- when it appears, that it reconfigures them when a node.features.audio.*
-- setting changes, and that it leaves alone the streams whose ports were never
-- configured, such as a client stream that is waiting to be linked.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("device-node", "Audio/Sink")

local started = false

-- The device stream was configured when it appeared, so a settings change
-- reaches it. The client stream has no target to be linked with, so its ports
-- are never configured.
local function reconfigureStreams (device, client)
  assert (client:get_ports_state () == AudioStreamPortsState.UNCONFIGURED)

  tu.setSetting ("node.features.audio.monitor-ports", "false", function ()
    tu.waitUntil (function ()
      return not device:has_monitor_ports_enabled ()
    end, function ()
      Log.info (device, "monitor ports disabled by the settings change")
      assert (client:get_ports_state () ==
          AudioStreamPortsState.UNCONFIGURED)
      Script:finish_activation ()
    end)
  end)
end

SimpleEventHook {
  name = "stream-added@test-configure-ports-settings",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local name = stream:get_properties () ["node.name"]

    if name == "device-node" and not started then
      started = true

      tu.waitUntil (function ()
        return stream:get_ports_state () == AudioStreamPortsState.CONFIGURED
      end, function ()
        assert (stream:has_monitor_ports_enabled ())
        tu.createStreamNode ("playback", {
          ["target.object"] = "missing-node",
          ["node.dont-fallback"] = "true",
        })
      end)
    elseif name == "stream-node" then
      assert (stream:get_ports_state () == AudioStreamPortsState.UNCONFIGURED)
      reconfigureStreams (tu.streams ["device-node"], stream)
    end
  end
}:register ()
