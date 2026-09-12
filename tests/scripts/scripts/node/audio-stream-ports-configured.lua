-- Tests the 'ports-configured' signal of an audio stream: it must be emitted
-- exactly once per configuration requested through the stream, and not at all
-- for PortConfig changes that the stream did not request.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("device-node", "Audio/Sink")

local count = 0
local started = false

-- Changes the PortConfig of the stream's node behind the stream's back, the
-- way another client would. No configuration was requested through the stream,
-- so 'ports-configured' must not be emitted, no matter how many PortConfig and
-- port changes the node reports.
local function changePortConfigExternally (stream)
  local node = stream:get_node ()

  -- PortConfig can only be set while the node is suspended
  node:send_command ("Suspend")
  node:set_param ("PortConfig", Pod.Object {
    "Spa:Pod:Object:Param:PortConfig", "PortConfig",
    direction = "Input",
    mode = "dsp",
    monitor = true,
    control = false,
    format = stream:get_ports_format (),
  })

  -- the stream picks the change up and updates its own PortConfig info; that
  -- is the point at which it must not report the ports as configured
  tu.waitUntil (function () return stream:has_monitor_ports_enabled () end,
    function ()
      Log.info ("ports-configured emitted " .. tostring (count) ..
          " time(s) on an unrequested PortConfig change")
      assert (count == 0)
      Script:finish_activation ()
    end)
end

-- Configures the stream's ports. The node reports the change more than once,
-- but 'ports-configured' must be emitted exactly once.
local function configureStream (stream)
  assert (stream:configure_ports_with_props { ["stream.monitor-ports"] = "false" })

  tu.waitUntil (function () return count > 0 end, function ()
    assert (stream:has_ports_configured ())
    assert (not stream:has_monitor_ports_enabled ())

    -- give the remaining PortConfig and port changes time to arrive; none of
    -- them may emit the signal again
    Core.timeout_add (500, function ()
      Log.info ("ports-configured emitted " .. tostring (count) ..
          " time(s) on a requested configuration")
      assert (count == 1)

      count = 0
      changePortConfigExternally (stream)
      return false
    end)
  end)
end

SimpleEventHook {
  name = "stream-added@test-ports-configured",
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
    end)

    configureStream (stream)
  end
}:register ()
