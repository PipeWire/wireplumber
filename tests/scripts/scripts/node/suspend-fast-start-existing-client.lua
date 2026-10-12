-- Tests that a client already connected when the event source starts is
-- honored.

local tu = require ("test-utils")
local st = require ("suspend-test-utils")

Script.async_activation = true

st.watchdog (5000)
st.connectFastStartClients (1, st.capture, function ()
  -- announces the connected client again, as at startup
  st.connectFastStartClients (0, st.capture, function ()
    st.addDevice ("capture-device", "Audio/Source", {
      ["device.api"] = "alsa",
      ["node.slow-resume"] = "true",
      ["session.suspend-timeout-seconds"] = "0.3",
    })
  end)
  tu.restartPlugin ("standard-event-source")
end)

st.on_idle = function ()
  st.after (600, function ()
    assert (st.state ("capture-device") == "idle")
    Script:finish_activation ()
  end)
end

st.on_suspended = function (name)
  assert (false, name .. " was suspended")
end
