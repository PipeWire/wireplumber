-- Tests that an idle slow-resume source is not suspended while a client with
-- "session.fast-capture-start" is connected, and that it is suspended after its
-- suspend timeout once the client disconnects.

local tu = require ("test-utils")
local st = require ("suspend-test-utils")

Script.async_activation = true

local released = false

st.watchdog (5000)
st.connectFastStartClients (1, st.capture, function ()
  st.addDevice ("capture-device", "Audio/Source", {
    ["device.api"] = "alsa",
    ["node.slow-resume"] = "true",
    ["session.suspend-timeout-seconds"] = "0.3",
  })
end)

st.on_idle = function ()
  st.after (600, function ()
    assert (st.state ("capture-device") == "idle")
    released = true
    tu.disconnectClient ()
  end)
end

st.on_suspended = function (name)
  assert (name == "capture-device")
  assert (released)
  Script:finish_activation ()
end
