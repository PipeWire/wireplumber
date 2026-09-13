-- Tests that a smart filter that gets disabled at runtime is unlinked from the
-- target it had picked, and that the client stream that was linked through it
-- is moved to the real sink.

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

filter_stream_created = false
client_stream_created = false

-- hook to create the stream side of the filter and then the client stream,
-- each one after the previously created nodes are ready
SimpleEventHook {
  name = "stream-added@test-linking",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    if not tu.streamsReady () then
      return
    end

    if not filter_stream_created then
      filter_stream_created = true
      tu.createStreamNode ("playback", {
        ["node.name"] = "filter-stream",
        ["node.link-group"] = "filter-group",
      })
    elseif not client_stream_created then
      client_stream_created = true
      tu.createStreamNode ("playback", {
        ["node.name"] = "client-stream",
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

    if stream:get_properties () ["node.name"] ~= "client-stream" or disabled then
      return
    end

    local filter_stream = tu.streams ["filter-stream"]
    local filter_sink = tu.streams ["filter-sink"]
    local device_node = tu.streams ["default-device-node"]

    -- the client stream goes through the filter
    assert (target == filter_sink)
    assert (tu.getStreamLink (stream, filter_sink) ~= nil)
    assert (tu.getStreamLink (filter_stream, device_node) ~= nil)

    disabled = true
    filters_metadata:set (filter_sink:get_node_bound_id (),
        "filter.smart.disabled", "Spa:String:JSON", "true")

    -- the filter is unlinked from the device node and the client stream is
    -- moved from the filter to the device node
    tu.waitUntil (function ()
      return tu.getStreamLink (filter_stream, device_node) == nil and
          tu.getStreamLink (stream, device_node) ~= nil and
          tu.getStreamLink (stream, filter_sink) == nil
    end, function ()
      Script:finish_activation ()
    end)
  end
}:register ()
