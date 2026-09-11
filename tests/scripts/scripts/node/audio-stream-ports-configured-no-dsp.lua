-- Tests the 'ports-configured' signal for a configuration that puts the node
-- in no-dsp mode. The node advertises a PortConfig param change only as a
-- side effect of its follower's Format changing, which does not happen in
-- no-dsp mode, so the signal must not depend on being notified.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("device-node", "Audio/Sink")

local count = 0
local started = false

SimpleEventHook {
  name = "stream-added@test-ports-configured-no-dsp",
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

    stream:connect ("ports-configured", function ()
      count = count + 1
      Log.info (stream, "ports configured, count=" .. tostring (count))
      assert (count == 1)
      assert (stream:has_ports_configured ())
      Script:finish_activation ()
    end)

    assert (stream:configure_ports_with_props { ["stream.no-dsp"] = "true" })
  end
}:register ()
