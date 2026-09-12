-- Tests that node/configure-ports.lua reconfigures the audio streams when a
-- node.features.audio.* setting changes, and that it leaves alone the streams
-- whose ports we have not configured yet.

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

local function setSetting (name, value, cb)
  tu.settings_metadata:set (0, name, "Spa:String:JSON", value)
  waitUntil (function ()
    return tostring (Settings.get_boolean (name)) == value
  end, cb)
end

-- A settings change reaches the streams that have a port configuration.
local function reconfigureStream (stream)
  setSetting ("node.features.audio.monitor-ports", "false", function ()
    waitUntil (function ()
      return not stream:has_monitor_ports_enabled ()
    end, function ()
      Log.info (stream, "monitor ports disabled by the settings change")
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

    if started then
      return
    end
    started = true

    assert (not stream:has_ports_configured ())

    -- a stream we have not configured yet is left alone
    setSetting ("node.features.audio.control-port", "true", function ()
      assert (not stream:has_ports_configured ())

      stream:connect ("ports-configured", function ()
        assert (stream:has_control_ports_enabled ())
        reconfigureStream (stream)
      end)
      assert (stream:configure_ports_with_props ())
    end)
  end
}:register ()
