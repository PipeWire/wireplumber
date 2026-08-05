-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- looks for changes in user-preferences and devices added/removed and schedules
-- rescan and pushes "select-default-node" event for each of the media_classes

nutils = require ("node-utils")

log = Log.open_topic ("s-default-nodes")

-- looks for changes in user-preferences and devices added/removed and schedules
-- rescan
SimpleEventHook {
  name = "default-nodes/rescan-trigger",
  interests = {
    EventInterest {
      Constraint { "event.type", "c", "node-added", "node-removed" },
      Constraint { "media.class", "#", "Audio/*" },
    },
    EventInterest {
      Constraint { "event.type", "c", "node-added", "node-removed" },
      Constraint { "media.class", "#", "Video/*" },
    },
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "c", "default.configured.audio.sink",
          "default.configured.audio.source", "default.configured.video.source"
      },
    },
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed"},
      Constraint { "event.subject.param-id", "c", "Route", "EnumRoute"},
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("schedule-rescan", "default-nodes")
  end
}:register ()

-- pushes "select-default-node" event for each of the media_classes
SimpleEventHook {
  name = "default-nodes/rescan",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "rescan-for-default-nodes" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local nodes_om = source:call ("get-object-manager", "node")
    local devices_om = source:call ("get-object-manager", "device")

    log:trace ("re-evaluating default nodes")

    -- Audio Sink
    pushSelectDefaultNodeEvent (source, nodes_om, devices_om, "audio.sink", {
      "Audio/Sink", "Audio/Duplex"
    })

    -- Audio Source
    pushSelectDefaultNodeEvent (source, nodes_om, devices_om, "audio.source", {
      "Audio/Source", "Audio/Source/Virtual", "Audio/Duplex", "Audio/Sink"
    })

    -- Video Source
    pushSelectDefaultNodeEvent (source, nodes_om, devices_om, "video.source", {
      "Video/Source", "Video/Source/Virtual"
    })
  end
}:register ()

function pushSelectDefaultNodeEvent (source, nodes_om, devices_om, def_node_type,
                                     media_classes)
  local nodes =
      collectAvailableNodes (nodes_om, devices_om, media_classes)
  local event = source:call ("create-event", "select-default-node", nil, {
      ["default-node.type"] = def_node_type,
  })
  event:set_data ("available-nodes", Json.Array (nodes))
  EventDispatcher.push_event (event)
end

-- Return an array table where each element is another table containing all the
-- node properties of all the nodes that can be selected for a given media class
-- set and direction
function collectAvailableNodes (nodes_om, devices_om, media_classes)
  local collected = {}

  for node in nodes_om:iterate {
    type = "Node",
    Constraint { "media.class", "c", table.unpack (media_classes) },
  } do
    -- if the node is associated to a real device, check that the node has
    -- available routes
    if not nutils.has_available_routes (node, devices_om) then
      goto next_node
    end

    table.insert (collected, Json.Object (node.properties))
    ::next_node::
  end

  return collected
end
