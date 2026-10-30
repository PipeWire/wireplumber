-- Tests that the node id of a removed target stream is still available from
-- the stream properties. The node is unbound before the stream-removed event
-- is dispatched, so get_node_bound_id() cannot be used there; this is what
-- linking/pause-mpris relies on.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink")

node_removed = false
node_id = nil

SimpleEventHook {
  name = "stream-added@test-removed-node-id",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()

    if node_removed or not tu.streamsReady () then
      return
    end

    node_removed = true
    node_id = stream:get_node_bound_id ()
    assert (node_id ~= nil)
    stream:get_node ():request_destroy ()
  end
}:register ()

SimpleEventHook {
  name = "stream-removed@test-removed-node-id",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-removed" },
      Constraint { "kind", "=", StreamKind.TARGET, type = "gobject" },
      Constraint { "direction", "=", Direction.INPUT, type = "gobject" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()

    assert (tonumber (stream:get_properties () ["object.id"]) == node_id)
    assert (stream:get_node_bound_id () ~= node_id)

    Script:finish_activation ()
  end
}:register ()
