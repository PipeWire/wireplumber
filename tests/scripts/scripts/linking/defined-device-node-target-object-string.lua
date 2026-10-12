-- Tests linking of streams and defined devices. Three device nodes are created,
-- among which only one is selected as the defined device(target.object).

-- The target.object here is a string
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
      local props = {
        ["target.object"] = tu.lnkbls ["defined-device-node"].properties ["object.serial"]
      }
      tu.createStreamNode ("playback", props)
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
    assert (target.properties ["node.name"] == "defined-device-node")
    assert ((link:get_active_features () & Feature.SessionItem.ACTIVE) ~= 0)

    Script:finish_activation ()
  end
}:register ()
