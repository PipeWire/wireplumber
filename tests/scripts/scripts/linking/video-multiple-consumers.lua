-- Tests that a video stream can be linked to a camera that another video
-- stream is already linked to: video input ports cannot mix, but a video
-- output can feed many consumers.
--
-- The camera is a client stream with media.class set to Video/Source,
-- because a videotestsrc device node aborts the fixture at teardown.

local tu = require ("test-utils")

Script.async_activation = true

tu.createStreamNode ("video-playback", {
  ["node.name"] = "video-source-node",
  ["media.class"] = "Video/Source",
})

-- hook to create the first consumer, after the camera is ready
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
        ["node.name"] = "video-consumer-1",
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

    if name == "video-consumer-1" then
      if target == nil then
        return
      end

      local link = tu.getStreamLink (stream, target)
      assert (link ~= nil)
      assert (link:get_active_features () ~= 0)
      assert (StreamLinkManager.get_peers_count (target) == 1)

      -- create the second consumer only now, so that the camera is already
      -- linked when this one is prepared
      tu.createStreamNode ("video-capture", {
        ["node.name"] = "video-consumer-2",
        ["target.object"] = "video-source-node",
      })
    elseif name == "video-consumer-2" then
      assert (target ~= nil, "the already linked camera was refused")

      local link = tu.getStreamLink (stream, target)
      assert (link ~= nil)
      assert (target.properties ["node.name"] == "video-source-node")
      assert (link:get_active_features () ~= 0)
      assert (StreamLinkManager.get_peers_count (target) == 2)

      Script:finish_activation ()
    end
  end
}:register ()
