-- Tests that a video stream that advertises no EnumFormat at all, such as
-- pipewiresrc with ANY caps, is still activated, linked to its target and
-- gets a format.
--
-- The camera is a client stream with media.class set to Video/Source,
-- because a videotestsrc device node aborts the fixture at teardown. It is a
-- driver, like a v4l2 camera, so that the link takes its format.

local tu = require ("test-utils")

Script.async_activation = true

waiting = false

tu.createStreamNode ("video-playback", {
  ["node.name"] = "video-source-node",
  ["media.class"] = "Video/Source",
  ["node.driver"] = "true",
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
      tu.createStreamNode ("video-any-capture", {
        ["node.name"] = "video-any-consumer",
        ["target.object"] = "video-source-node",
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

    if name ~= "video-any-consumer" or waiting then
      return
    end

    assert (stream:get_supported_raw_format_count () == 0)
    assert (target ~= nil, "the stream with no EnumFormat was not linked")
    assert (target.properties ["node.name"] == "video-source-node")

    waiting = true

    tu.waitUntil (function ()
      assert (tu.getStreamError ("video-any-consumer") == nil)
      return tu.streamHasFormat ("video-any-consumer")
    end, function ()
      Script:finish_activation ()
    end)
  end
}:register ()
