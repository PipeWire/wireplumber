-- Tests that node/configure-ports.lua configures the ports of the new audio
-- streams that no link configures right away: the sink and the source of a pro
-- audio device, which share a node.group and only run together, and a client
-- stream that is not autoconnected.

local tu = require ("test-utils")

Script.async_activation = true

local group_props = { ["node.group"] = "pro-audio-0" }

tu.createDeviceNode ("pro-audio-sink", "Audio/Sink", group_props)
tu.createDeviceNode ("pro-audio-source", "Audio/Source", group_props)

local stream_created = false

SimpleEventHook {
  name = "stream-added@test-configure-ports-stream-added",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    if stream_created or not tu.streamsReady () then
      return
    end
    stream_created = true

    tu.createStreamNode ("playback", { ["node.autoconnect"] = "false" })

    -- has_ports_configured () is TRUE as soon as the request is sent, so wait
    -- for the ports of the new configuration as well
    local function isConfigured (stream)
      return stream ~= nil and stream:has_ports_configured () and
          stream:get_ports_count () > 0
    end

    tu.waitUntil (function ()
      return isConfigured (tu.streams ["stream-node"]) and
          isConfigured (tu.streams ["pro-audio-sink"]) and
          isConfigured (tu.streams ["pro-audio-source"])
    end, function ()
      for _, name in ipairs { "pro-audio-sink", "pro-audio-source",
          "stream-node" } do
        local stream = tu.streams [name]
        assert (stream:get_ports_mode () == "dsp", string.format (
            "%s has ports mode %s", name, tostring (stream:get_ports_mode ())))
      end

      Script:finish_activation ()
    end)
  end
}:register ()
