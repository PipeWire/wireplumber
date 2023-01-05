-- Tests linking of streams and defined devices. Here in this test case we
-- update defined device in both the node properties and in metadata.

-- The device in metadata takes priority over the one in node properties.
local pu = require ("policy-utils")
local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("nondefault-device-node", "Audio/Sink")
tu.createDeviceNode ("default-device-node", "Audio/Sink")
tu.createDeviceNode ("defined-device-node-in-props", "Audio/Sink")
tu.createDeviceNode ("defined-device-node-in-metadata", "Audio/Sink")

tu.createStreamNode ("stream-node")

-- hook to selet defined target
SimpleEventHook {
  name = "linkable-added@test-linking",
  after = "linkable-added@test-utils-linking",
  interests = {
    -- on linkable added or removed, where linkable is adapter or plain node
    EventInterest {
      Constraint { "event.type", "=", "session-item-added" },
      Constraint { "event.session-item.interface", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    },
  },
  execute = function (event)
    if tu.linkables_ready () then
      tu.set_target_in_stream ("target.object", "defined-device-node-in-props")
      tu.set_target_in_metadata ("target.node", "defined-device-node-in-metadata")
    end
  end
}:register ()

SimpleEventHook {
  name = "linking/test-linking",
  after = "linking/link-target",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source, om, si, si_props, si_flags, target =
        pu:unwrap_find_target_event (event)

    if not target then
      return
    end

    Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
        tostring (si_props ["node.name"]),
        tostring (si_props ["node.id"]), si.id))

    local link = pu.lookupLink (si.id, si_flags.peer_id)
    assert (link ~= nil)
    assert (si_props ["node.name"] == "stream-node")
    assert (target.properties ["node.name"] == "defined-device-node-in-metadata")
    assert ((link:get_active_features () & Feature.SessionItem.ACTIVE) ~= 0)

    Script:finish_activation ()
  end
}:register ()
