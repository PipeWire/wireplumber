-- Tests that a smart filter that gets disabled at runtime is unlinked from the
-- target it had picked.

local tu = require ("test-utils")

Script.async_activation = true

filters_metadata = ImplMetadata ("filters")
filters_metadata:activate (Features.ALL, function (m, e)
  assert (e == nil, tostring (e))

  tu.createDeviceNode ("default-device-node", "Audio/Sink")
  tu.createDeviceNode ("filter-sink", "Audio/Sink", {
    ["node.link-group"] = "filter-group",
    ["filter.smart"] = "true",
    ["filter.smart.name"] = "test-filter",
  })
end)

-- hook to create the stream side of the filter, after both device nodes are ready
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

    if tu.streamsReady () and
        stream:get_properties () ["node.name"] ~= "filter-stream" then
      tu.createStreamNode ("playback", {
        ["node.name"] = "filter-stream",
        ["node.link-group"] = "filter-group",
      })
    end
  end
}:register ()

disabled = false

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

    if stream:get_properties () ["node.name"] ~= "filter-stream" or disabled then
      return
    end

    assert (target ~= nil)
    assert (target == tu.streams ["default-device-node"])
    assert (tu.getStreamLink (stream, target) ~= nil)

    disabled = true

    local filter_sink = tu.streams ["filter-sink"]
    filters_metadata:set (filter_sink:get_node_bound_id (),
        "filter.smart.disabled", "Spa:String:JSON", "true")

    tu.waitUntil (function ()
      return tu.getStreamLink (stream, target) == nil
    end, function ()
      Script:finish_activation ()
    end)
  end
}:register ()
