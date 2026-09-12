-- Tests that a stream which is moved to another target is first unlinked from
-- the one it was linked to, so that it ends up with exactly one link.

local tu = require ("test-utils")

Script.async_activation = true

moved = false

tu.createDeviceNode ("default-device-node", "Audio/Sink")
tu.createDeviceNode ("defined-device-node", "Audio/Sink")

-- hook to create the stream node, after the device nodes are ready
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
      tu.createStreamNode ("playback")
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

    if stream:get_properties () ["node.name"] ~= "stream-node" then
      return
    end

    if not moved then
      if target == nil then
        return
      end

      assert (target.properties ["node.name"] == "default-device-node")
      assert (tu.getStreamLink (stream, target) ~= nil)

      moved = true
      tu.setTargetInMetadata ("target.object", "defined-device-node",
          "object.serial")
      return
    end

    assert (target ~= nil, "the stream was not moved")
    assert (target.properties ["node.name"] == "defined-device-node")
    assert (tu.getStreamLink (stream, target) ~= nil)

    local old_target = tu.streams ["default-device-node"]
    assert (StreamLinkManager.get_peers_count (old_target) == 0,
        "the stream is still linked to its previous target")
    assert (StreamLinkManager.get_peers_count (stream) == 1)

    Script:finish_activation ()
  end
}:register ()
