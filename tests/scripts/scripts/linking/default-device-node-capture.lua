-- Tests linking of streams and default devices. Two device nodes are setup,
-- among which only one is selected as default device node.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("nondefault-device-node", "Audio/Source")
tu.createDeviceNode ("default-device-node", "Audio/Source")

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
      tu.createStreamNode ("capture")
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
    assert (target.properties ["node.name"] == "default-device-node")
    assert (link:get_active_features () ~= 0)

    assert (target == tu.streams ["default-device-node"])

    Script:finish_activation ()
  end
}:register ()
