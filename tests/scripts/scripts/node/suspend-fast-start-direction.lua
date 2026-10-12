-- Tests that "session.fast-playback-start" keeps slow-resume sinks from
-- suspending, but not slow-resume sources.

local st = require ("suspend-test-utils")

Script.async_activation = true

st.watchdog (5000)
st.connectFastStartClients (1, st.playback, function ()
  st.addDevice ("slow-sink", "Audio/Sink", {
    ["device.api"] = "alsa",
    ["node.slow-resume"] = "true",
    ["session.suspend-timeout-seconds"] = "0.3",
  })
  st.addDevice ("slow-source", "Audio/Source", {
    ["device.api"] = "alsa",
    ["node.slow-resume"] = "true",
    ["session.suspend-timeout-seconds"] = "0.3",
  })
end)

st.on_idle = function () end

st.on_suspended = function (name)
  assert (name == "slow-source")
  st.after (600, function ()
    assert (st.state ("slow-sink") == "idle")
    Script:finish_activation ()
  end)
end
