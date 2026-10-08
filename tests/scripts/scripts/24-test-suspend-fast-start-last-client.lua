-- Tests that a node is suspended only once the last client asking for it
-- disconnects.

local tu = require ("test-utils")
local st = require ("suspend-test-utils")

Script.async_activation = true

local released = false

st.watchdog (5000)
st.connectFastStartClients (2, st.capture_and_playback, function ()
  st.addDevice ("capture-device", "Audio/Source", {
    ["device.api"] = "alsa",
    ["node.slow-resume"] = "true",
    ["session.suspend-timeout-seconds"] = "0.3",
  })
end)

st.on_idle = function ()
  st.after (100, function ()
    tu.disconnectClient ()
  end)
  st.after (700, function ()
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
