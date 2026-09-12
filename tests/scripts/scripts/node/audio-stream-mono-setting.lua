-- Tests that node.features.audio.mono only applies to hardware audio sinks.
-- The device node here is a null-audio-sink, so enabling the setting must not
-- configure its stream in mono.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("device-node", "Audio/Sink")

local started = false

-- Calls cb once condition() holds. Fails the test if it never does.
local function waitUntil (condition, cb)
  local attempts = 0

  Core.timeout_add (10, function ()
    if condition () then
      cb ()
      return false
    end
    attempts = attempts + 1
    assert (attempts < 500)
    return true
  end)
end

SimpleEventHook {
  name = "stream-added@test-mono-setting",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()

    if started then
      return
    end
    started = true

    tu.settings_metadata:set (0, "node.features.audio.mono",
        "Spa:String:JSON", "true")

    waitUntil (function ()
      return Settings.get_boolean ("node.features.audio.mono")
    end, function ()
      stream:connect ("ports-configured", function ()
        local format = stream:get_ports_format ():parse ()

        Log.info (stream, "configured with " ..
            tostring (format.properties.channels) .. " channel(s)")
        assert (format.properties.channels > 1)
        Script:finish_activation ()
      end)

      -- monitor-ports is enabled by default, so this always reconfigures
      assert (stream:configure_ports_with_props {
        ["stream.monitor-ports"] = "false"
      })
    end)
  end
}:register ()
