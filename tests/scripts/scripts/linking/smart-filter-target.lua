-- Tests that a smart filter with filter.smart.target only applies to streams
-- going to that target. Other streams keep using the default device.

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
    tu.createDeviceNode ("other-device-node", "Audio/Sink")
  end,
  function ()
    tu.createDeviceNode ("filter-sink", "Audio/Sink", {
      ["node.link-group"] = "filter-group",
      ["filter.smart"] = "true",
      ["filter.smart.target"] = "{ \"node.name\": \"other-device-node\" }",
    })
  end,
  function ()
    tu.createStreamNode ("playback", {
      ["node.name"] = "filter-stream",
      ["node.link-group"] = "filter-group",
    })
  end,
  function ()
    tu.createStreamNode ("playback", { ["node.name"] = "default-client" })
  end,
  function ()
    tu.createStreamNode ("playback", {
      ["node.name"] = "other-client",
      ["node.target"] = tu.streams ["other-device-node"]:get_node_bound_id (),
    })
    tu.waitUntil (function ()
      return tu.streamsReady () and
          linked ("default-client", "default-device-node") and
          linked ("other-client", "filter-sink") and
          linked ("filter-stream", "other-device-node")
    end, function ()
      assert (not linked ("default-client", "filter-sink"))
      assert (not linked ("other-client", "other-device-node"))
      Script:finish_activation ()
    end)
  end,
}

tu.createDeviceNode ("default-device-node", "Audio/Sink")
