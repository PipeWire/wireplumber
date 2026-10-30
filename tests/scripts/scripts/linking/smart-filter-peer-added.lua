-- Tests that the main stream of a smart filter is not a filter until its peer is
-- added: a client stream links to the default device, and is moved to the filter
-- once the stream side of the filter appears.

local tu = require ("test-utils")

Script.async_activation = true

-- creates each node once the previously created ones are ready
next_step = 1

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

local function linked (a, b)
  return tu.getStreamLink (tu.streams [a], tu.streams [b]) ~= nil
end

steps = {
  function ()
    tu.createDeviceNode ("filter-sink", "Audio/Sink", {
      ["node.link-group"] = "filter-group",
      ["filter.smart"] = "true",
    })
  end,
  function ()
    tu.createStreamNode ("playback", { ["node.name"] = "client-stream" })
    tu.waitUntil (function ()
      return tu.streamsReady () and
          linked ("client-stream", "default-device-node")
    end, function ()
      assert (not linked ("client-stream", "filter-sink"))

      tu.createStreamNode ("playback", {
        ["node.name"] = "filter-stream",
        ["node.link-group"] = "filter-group",
      })
      tu.waitUntil (function ()
        return tu.streamsReady () and
            linked ("client-stream", "filter-sink") and
            linked ("filter-stream", "default-device-node") and
            not linked ("client-stream", "default-device-node")
      end, function ()
        Script:finish_activation ()
      end)
    end)
  end,
}

tu.createDeviceNode ("default-device-node", "Audio/Sink")
