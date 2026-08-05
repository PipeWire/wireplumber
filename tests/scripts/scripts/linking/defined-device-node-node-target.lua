-- Tests linking of streams and defined devices. Three device nodes are created,
-- among which only one is selected as the defined device(using "node.target" property).

-- The "node.target" here is an integer
local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("nondefault-device-node", "Audio/Sink")
tu.createDeviceNode ("default-device-node", "Audio/Sink")
tu.createDeviceNode ("defined-device-node", "Audio/Sink")

-- hook to create stream node, stream is created after the device nodes are
-- ready
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
      local props = {
        ["node.target"] = tu.streams ["defined-device-node"]:get_node_bound_id ()
      }
      tu.createStreamNode ("playback", props)
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

    if target == nil then
      return
    end

    local stream_props = stream:get_properties ()
    local link = tu.getStreamLink (stream, target)
    assert (link ~= nil)
    assert (stream_props ["node.name"] == "stream-node")
    assert (target.properties ["node.name"] == "defined-device-node")
    assert (link:get_active_features () ~= 0)

    Script:finish_activation ()
  end
}:register ()
