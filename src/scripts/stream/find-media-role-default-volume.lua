-- WirePlumber
--
-- Copyright © 2025 Phosh.mobi e.V.
--    @author Guido Günther <agx@sigxcpu.org>
--
-- SPDX-License-Identifier: MIT
--
-- Select the media role default volume

log = Log.open_topic("s-stream")

local cutils = require ("common-utils")

function findHighestPriorityRoleStream (stream_om)
  local best_role = nil
  local best_stream = nil
  local best_prio = 0

  local default_role = Settings.get ("stream.default-media-role")
  if default_role then
    default_role = default_role:parse()
  end

  for stream in stream_om:iterate {
     type = "AudioStream",
     Constraint { "shape", "=", StreamShape.SINK, type = "gobject" },
     Constraint { "node.name", "#", "input.loopback.sink.role.*" },
  } do
     local stream_props = stream:get_properties ()
     local roles = stream_props["device.intended-roles"]
     local stream_name = stream_props ["node.name"]
     local prio = tonumber(stream_props ["policy.role-based.priority"])

     -- Use the stream that handles the default_role as fallback
     -- when no stream is in running state
     if best_role == nil and roles and default_role then
       local roles_table = Json.Raw(roles):parse()
       for i, v in ipairs (roles_table) do
         if default_role == v then
           best_role = stream_name
           best_stream = stream:get_port_group ()
           best_prio = prio
           break
         end
       end
     end

     -- Streams do not have a state, so the state of their node is used
     local node = stream:get_node ()
     if node and node.state == "running" then
       if prio > best_prio then
         best_role = stream_name
         best_stream = stream:get_port_group ()
         best_prio = prio
       end
     end
  end

  log:info (string.format ("Volume control is on : '%s' (stream '%s'), prio %d",
      best_role, best_stream, best_prio))

  -- The volume control is given as the node name and port group of the stream
  local metadata = cutils.get_default_metadata_object ()
  metadata:set (0, "current.role-based.volume.control", "Spa:String:JSON",
                Json.Object {
                  ["name"] = best_role,
                  ["stream"] = best_stream,
                }:to_string ())
end

SimpleEventHook {
  name = "stream/rescan-for-media-role-volume",
  interests = {
    EventInterest {
      Constraint  { "event.type", "=", "rescan-for-media-role-volume" }
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream_om = source:call ("get-object-manager", "stream")
    findHighestPriorityRoleStream (stream_om)
  end
}:register ()

-- Track best volume control for media role based priorities
SimpleEventHook {
  name = "stream/find-media-role-default-volume",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "shape", "=", StreamShape.SINK, type = "gobject" },
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
    source:call ("schedule-rescan", "media-role-volume")
  end
}:register ()
