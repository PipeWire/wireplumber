-- Tests linking of streams and non default devices. These devices nodes are
-- neither defined nor default.

local putils = require ("policy-utils")

Script.async_activation = true

local properties = {}
properties ["node.name"] = "nondefault-device-node"
properties ["media.class"] = "Audio/Sink"
properties ["factory.name"] = "support.null-audio-sink"

local node = Node ("adapter", properties)
node:activate (Feature.Proxy.BOUND, function (n)
  Log.info (node, "created and activated node: " .. properties ["node.name"])
end)

properties ["node.name"] = "stream-node"
properties ["media.class"] = "Stream/Output/Audio"
properties ["factory.name"] = "support.null-audio-sink"

node = Node ("adapter", properties)
node:activate (Feature.Proxy.BOUND, function (n)
  Log.info (node, "created and activated node: " .. properties ["node.name"])
end)

linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "active-features", "!", 0, type = "gobject" },
  }
}
linkables_om:activate ()

local si_target = nil
local si = nil

linkables_om:connect ("object-added", function (_, linkable)
  local lp = linkable.properties
  local name = lp ["node.name"]
  Log.info (linkable, "created and activated linkable: " .. name)

  if name == "nondefault-device-node" then
    si_target = linkable
  elseif name == "stream-node" then
    si = linkable
  end

  if si and si_target then
    EventDispatcher.push_event {
      type = "find-target-si-and-link", priority = 10, subject = si
    }
  end
end)

SimpleEventHook {
  name = "test-policy@nondefault-device-node",
  type = "after-events-with-event",
  priority = "policy-tests-hook",

  interests = {
    EventInterest {
      Constraint { "event.type", "=", "find-target-si-and-link" },
    },
  },

  execute = function (event)
    testPolicy (event)
  end
}:register ()

function testPolicy (event)
  local si = event:get_subject ()
  local si_props = si.properties
  local si_id = si.id;
  local si_flags = putils.get_flags (si_id)
  local si_target = nil

  Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
    tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

  local link = putils.lookupLink (si_id, si_flags.peer_id)
  assert (link ~= nil)
  Script:finish_activation ()
end
