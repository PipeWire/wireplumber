-- Tests that a stream which is itself the target of another stream's link does
-- not destroy that link when it looks for a target of its own and finds none.
--
-- The producer is a Stream/Output/Video client, so it is a subject stream and
-- the rescan asks it to find a Video/Sink, of which there is none; the consumer
-- reaches it by name in target.object.

local tu = require ("test-utils")

Script.async_activation = true

linked = false

tu.createStreamNode ("video-playback", {
  ["node.name"] = "video-producer",
})

-- hook to create the consumer, after the producer is ready
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

    if tu.streamsReady () and name == "video-producer" then
      tu.createStreamNode ("video-capture", {
        ["node.name"] = "video-consumer",
        ["target.object"] = "video-producer",
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
    local source = event:get_source ()
    local stream = event:get_subject ()
    local target = event:get_data ("target")
    local name = stream:get_properties () ["node.name"]

    if name == "video-consumer" then
      if target == nil then
        return
      end

      local link = tu.getStreamLink (stream, target)
      assert (link ~= nil)
      assert (target.properties ["node.name"] == "video-producer")
      assert (link:get_active_features () ~= 0)
      linked = true

      -- rescan, so that the producer looks for a target of its own while it
      -- already has the consumer as a peer
      source:call ("schedule-rescan", "linking")

    elseif name == "video-producer" and linked then
      assert (target == nil, "a target was found for the producer")

      local consumer = tu.streams ["video-consumer"]
      assert (StreamLinkManager.find_link (stream, consumer) ~= nil,
          "the consumer's link was destroyed")

      Script:finish_activation ()
    end
  end
}:register ()
