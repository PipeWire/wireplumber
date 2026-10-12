-- Tests that clients without "session.fast-capture-start" and
-- "session.fast-playback-start", or with them set to "false", don't keep nodes
-- from suspending.

local tu = require ("test-utils")
local st = require ("suspend-test-utils")

Script.async_activation = true

st.watchdog (5000)
tu.connectClient ({ ["application.name"] = "other-client" })
tu.connectClient ({
  ["session.fast-capture-start"] = "false",
  ["session.fast-playback-start"] = "false",
})
st.addDevice ("capture-device", "Audio/Source", {
  ["device.api"] = "alsa",
  ["node.slow-resume"] = "true",
  ["session.suspend-timeout-seconds"] = "0.3",
})

st.on_idle = function () end

st.on_suspended = function (name)
  assert (name == "capture-device")
  Script:finish_activation ()
end
