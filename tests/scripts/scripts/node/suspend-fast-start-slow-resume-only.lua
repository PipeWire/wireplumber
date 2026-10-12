-- Tests that only nodes with "node.slow-resume" are kept from suspending.

local st = require ("suspend-test-utils")

Script.async_activation = true

local suspended = {}

st.watchdog (5000)
st.connectFastStartClients (1, st.capture_and_playback, function ()
  st.addDevice ("slow-source", "Audio/Source", {
    ["device.api"] = "alsa",
    ["node.slow-resume"] = "true",
    ["session.suspend-timeout-seconds"] = "0.3",
  })
  st.addDevice ("capture-device", "Audio/Source", {
    ["device.api"] = "alsa",
    ["session.suspend-timeout-seconds"] = "0.3",
  })
  st.addDevice ("playback-device", "Audio/Sink", {
    ["device.api"] = "alsa",
    ["session.suspend-timeout-seconds"] = "0.3",
  })
end)

st.on_idle = function () end

st.on_suspended = function (name)
  assert (name ~= "slow-source")
  suspended [name] = true

  if suspended ["capture-device"] and suspended ["playback-device"] then
    st.after (600, function ()
      assert (st.state ("slow-source") == "idle")
      Script:finish_activation ()
    end)
  end
end
