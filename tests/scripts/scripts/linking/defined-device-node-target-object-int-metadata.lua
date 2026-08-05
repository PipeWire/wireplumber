-- Tests linking of streams and defined devices. Here in this test case we
-- update defined device in both the node properties and in metadata.

-- The device in metadata takes priority over the one in node properties.
local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("nondefault-device-node", "Audio/Sink")
tu.createDeviceNode ("default-device-node", "Audio/Sink")
tu.createDeviceNode ("defined-device-node-in-props", "Audio/Sink")
tu.createDeviceNode ("defined-device-node-in-metadata", "Audio/Sink")

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
      -- all streams created except stream-node
      local props = {
        ["target.object"] = tu.streams ["defined-device-node-in-props"].properties ["object.serial"]
      }
      tu.createStreamNode ("playback", props)
    elseif tu.streamsReady () and tu.streams ["stream-node"] then
      -- when "stream-node" is ready
      tu.setTargetInMetadata ("target.object", "defined-device-node-in-metadata", "object.serial")
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
    assert (link:get_active_features () ~= 0)
    if (target.properties ["node.name"] == "defined-device-node-in-metadata") then
      Script:finish_activation ()
    end
  end
}:register ()
