-- Tests linking of streams and defined devices. Three device nodes are created,
-- among which only one is selected as the defined device("target.node").

-- The "target.node" here is defined in default metadata.
local pu = require ("linking-utils")
local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("nondefault-device-node", "Audio/Sink")
tu.createDeviceNode ("default-device-node", "Audio/Sink")
tu.createDeviceNode ("defined-device-node", "Audio/Sink")

-- hook to create stream node, stream is created after the device nodes are
-- ready
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
    local lnkbl = event:get_subject ()
    local name = lnkbl.properties ["node.name"]

    if tu.linkablesReady () and name ~= "stream-node" then
      -- all linkables created except stream-node
      tu.createStreamNode ("playback")
    elseif tu.linkablesReady () and tu.lnkbls ["stream-node"] then
      -- when "stream-node" linkable is ready
      tu.setTargetInMetadata ("target.node", "defined-device-node")
    end
  end
}:register ()

-- hook to select defined target
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
    if tu.linkablesReady () then
      tu.setTargetInMetadata ("target.node", "defined-device-node")
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
    pu:unwrap_select_target_event (event)

    if not target then
      return
    end

    Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
      tostring (si_props ["node.name"]),
      tostring (si_props ["node.id"]), si.id))

    local link = pu.lookupLink (si.id, si_flags.peer_id)
    assert (link ~= nil)
    assert (si_props ["node.name"] == "stream-node")
    assert ((link:get_active_features () & Feature.SessionItem.ACTIVE) ~= 0)
    if (target.properties ["node.name"] == "defined-device-node") then
      Script:finish_activation ()
    end
  end
}:register ()
