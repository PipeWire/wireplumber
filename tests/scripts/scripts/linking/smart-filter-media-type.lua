-- Tests that streams of a different media type are not part of the same filter,
-- even if they share a link group: the smart filter is only the audio one.

local tu = require ("test-utils")
local stream_utils = require ("stream-utils")
local futils = require ("filter-utils")

Script.async_activation = true

-- creates each node once the previously created ones are ready
next_step = 1

steps = {
  function ()
    tu.createStreamNode ("playback", {
      ["node.name"] = "filter-stream",
      ["node.link-group"] = "filter-group",
    })
  end,
  function ()
    tu.createStreamNode ("video-playback", {
      ["node.name"] = "video-stream",
      ["node.link-group"] = "filter-group",
      ["media.class"] = "Video/Source",
    })
    tu.waitUntil (function ()
      return tu.streamsReady () and
          futils.is_filter_smart (Direction.INPUT, tu.streams ["filter-sink"])
    end, function ()
      local filter_sink = tu.streams ["filter-sink"]
      local filter_stream = tu.streams ["filter-stream"]
      local video_stream = tu.streams ["video-stream"]

      assert (stream_utils.is_same_filter (filter_stream, filter_sink))
      assert (not stream_utils.is_same_filter (video_stream, filter_sink))
      assert (not futils.is_filter_smart (Direction.INPUT, video_stream))
      Script:finish_activation ()
    end)
  end,
}

SimpleEventHook {
  name = "stream-added@test-linking",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    if tu.streamsReady () and next_step <= #steps then
      next_step = next_step + 1
      steps [next_step - 1] ()
    end
  end
}:register ()

tu.createDeviceNode ("filter-sink", "Audio/Sink", {
  ["node.link-group"] = "filter-group",
  ["filter.smart"] = "true",
})
