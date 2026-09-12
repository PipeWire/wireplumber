-- Tests that both clients are told when their link fails to negotiate a
-- format. Nothing in the policy compares video formats, so a consumer that
-- only accepts YUY2 is happily linked to a camera that only produces RGB;
-- the PipeWire link then goes to the error state and the clients have to
-- hear about it.
--
-- The camera is a client stream with media.class set to Video/Source,
-- because a videotestsrc device node aborts the fixture at teardown.

local tu = require ("test-utils")

Script.async_activation = true

tu.createStreamNode ("video-playback", {
  ["node.name"] = "video-source-node",
  ["media.class"] = "Video/Source",
})

-- hook to create the consumer, after the camera is ready
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

    if tu.streamsReady () and name == "video-source-node" then
      tu.createStreamNode ("video-capture", {
        ["node.name"] = "video-consumer",
        ["target.object"] = "video-source-node",
        ["test.video.format"] = "YUY2",
      })
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
    local name = stream:get_properties () ["node.name"]

    if name ~= "video-consumer" or target == nil then
      return
    end

    tu.waitUntil (function ()
      return tu.getStreamError ("video-consumer") ~= nil and
          tu.getStreamError ("video-source-node") ~= nil
    end, function ()
      Script:finish_activation ()
    end)
  end
}:register ()
