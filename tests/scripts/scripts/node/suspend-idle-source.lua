-- Tests that an idle ALSA capture source is suspended after its suspend
-- timeout.

local st = require ("suspend-test-utils")

Script.async_activation = true

st.watchdog (5000)
st.addDevice ("capture-device", "Audio/Source", {
  ["device.api"] = "alsa",
  ["session.suspend-timeout-seconds"] = "0.3",
})

st.on_idle = function ()
  st.after (100, function ()
    assert (st.state ("capture-device") == "idle")
  end)
end

st.on_suspended = function (name)
  assert (name == "capture-device")
  Script:finish_activation ()
end
