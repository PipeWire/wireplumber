-- Tests that when a client disconnects, only the nodes that no remaining
-- client asks for are suspended.

local tu = require ("test-utils")
local st = require ("suspend-test-utils")

Script.async_activation = true

st.watchdog (5000)
st.connectFastStartClients (1, st.capture, function ()
  st.connectFastStartClients (1, st.playback, function ()
    st.addDevice ("slow-source", "Audio/Source", {
      ["device.api"] = "alsa",
      ["node.slow-resume"] = "true",
      ["session.suspend-timeout-seconds"] = "0.3",
    })
    st.addDevice ("slow-sink", "Audio/Sink", {
      ["device.api"] = "alsa",
      ["node.slow-resume"] = "true",
      ["session.suspend-timeout-seconds"] = "0.3",
    })
  end)
end)

st.on_idle = function ()
  st.after (100, function ()
    -- the playback client
    tu.disconnectClient ()
  end)
end

st.on_suspended = function (name)
  assert (name == "slow-sink")
  st.after (600, function ()
    assert (st.state ("slow-source") == "idle")
    Script:finish_activation ()
  end)
end
