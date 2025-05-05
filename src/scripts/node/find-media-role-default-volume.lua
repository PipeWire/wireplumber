-- WirePlumber
--
-- Copyright © 2025 Phosh.mobi e.V.
--    @author Guido Günther <agx@sigxcpu.org>
--
-- SPDX-License-Identifier: MIT
--
-- Select the media role default volume

log = Log.open_topic("s-node")

local cutils = require ("common-utils")

function findHighestPriorityRoleNode (node_om)
  local best_role = nil
  local best_prio = 0

  local default_role = Settings.get ("node.stream.default-media-role")
  if default_role then
    default_role = default_role:parse()
  end

  for ni in node_om:iterate {
     type = "node",
     Constraint { "media.class", "=", "Audio/Sink" },
     Constraint { "node.name", "#", "input.loopback.sink.role.*" },
  } do
     local ni_props = ni.properties
     local roles = ni_props["device.intended-roles"]
     local node_name = ni_props ["node.name"]
     local prio = tonumber(ni_props ["policy.role-based.priority"])

     -- Use the node that handles the default_role as fallback
     -- when no node is in running state
     if best_role == nil and roles and default_role then
       local roles_table = Json.Raw(roles):parse()
       for i, v in ipairs (roles_table) do
         if default_role == v then
           best_role = node_name
           best_prio = prio
           break
         end
       end
     end

     if ni.state == "running" then
       if prio > best_prio then
         best_role = node_name
         best_prio = prio
       end
     end
  end

  log:info (string.format ("Volume control is on : '%s', prio %d", best_role, best_prio))
  local metadata = cutils.get_default_metadata_object ()
  metadata:set (0, "current.role-based.volume.control", "Spa:String:JSON",
                Json.Object { ["name"] = best_role }:to_string ())
end

SimpleEventHook {
  name = "node/rescan-for-media-role-volume",
  interests = {
    EventInterest {
      Constraint  { "event.type", "=", "rescan-for-media-role-volume" }
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local node_om = source:call ("get-object-manager", "node")
    findHighestPriorityRoleNode (node_om)
  end
}:register ()

-- Track best volume control for media role based priorities
SimpleEventHook {
  name = "node/find-media-role-default-volume",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "=", "Audio/Sink" },
      Constraint { "node.name", "#", "input.loopback.sink.role.*" }
    },
    EventInterest {
      Constraint { "event.type", "=", "node-state-changed" },
      Constraint { "media.class", "=", "Audio/Sink" },
      Constraint { "node.name", "#", "input.loopback.sink.role.*" }
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local node_om = source:call ("get-object-manager", "node")
    source:call ("schedule-rescan", "media-role-volume")
  end
}:register ()
