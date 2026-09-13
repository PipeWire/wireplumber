-- Tests that a new stream is linked without a rescan. This test runs with
-- linking/rescan-on-stream.lua not loaded, so nothing schedules a rescan when
-- the stream is added and linking/stream-added-immediate is the only thing
-- that can link it.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink")

-- hook to create the stream, after the device node is ready
SimpleEventHook {
  name = "stream-added@test-linking",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local name = stream:get_properties () ["node.name"]

    if tu.streamsReady () and name ~= "stream-node" then
      tu.createStreamNode ("playback")
    end
  end
}:register ()

SimpleEventHook {
  name = "linking/test-linking",
  after = "linking/create-stream-link",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local target = event:get_data ("target")

    if stream:get_properties () ["node.name"] ~= "stream-node" then
      return
    end

    assert (target ~= nil)
    assert (target == tu.streams ["default-device-node"])
    assert (tu.getStreamLink (stream, target) ~= nil)

    Script:finish_activation ()
  end
}:register ()
