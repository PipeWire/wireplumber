-- Tests that a client connecting while a suspend is pending cancels it.

local tu = require ("test-utils")
local st = require ("suspend-test-utils")

Script.async_activation = true

local released = false

st.watchdog (5000)
st.addDevice ("capture-device", "Audio/Source", {
  ["device.api"] = "alsa",
  ["node.slow-resume"] = "true",
  ["session.suspend-timeout-seconds"] = "0.6",
})

st.on_idle = function ()
  st.connectFastStartClients (1, st.capture, function ()
    -- the client arrived within the timeout; wait past it
    assert (st.state ("capture-device") == "idle")
    st.after (900, function ()
      assert (st.state ("capture-device") == "idle")
      released = true
      tu.disconnectClient ()
    end)
  end)
end

st.on_suspended = function (name)
  assert (name == "capture-device")
  assert (released)
  Script:finish_activation ()
end
