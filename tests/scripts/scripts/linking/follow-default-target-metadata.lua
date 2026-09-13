-- Tests that a target defined in the default metadata is left alone when it
-- happens to be the default sink. Only a target defined in the node properties
-- is replaced with -1 to follow the default.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink")

-- hook to create the stream node, once the device node is ready
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

metadata_set = false

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

    if stream:get_properties () ["node.name"] ~= "stream-node" then
      return
    end

    local target = tu.streams ["default-device-node"]
    assert (tu.getStreamLink (stream, target) ~= nil)

    -- pin the stream to the node that is also the default sink; this triggers
    -- another rescan, which is what would overwrite the pin
    if not metadata_set then
      metadata_set = true
      tu.setTargetInMetadata ("target.node", "default-device-node", "node.id")
      return
    end

    assert (tu.default_metadata:find (stream:get_node_bound_id (),
        "target.node") == tostring (target:get_node_bound_id ()))
    Script:finish_activation ()
  end
}:register ()
