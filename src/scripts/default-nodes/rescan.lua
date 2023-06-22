-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- looks for changes in user-preferences and devices added/removed and schedules
-- rescan and pushes "select-default-node" event for each of the media_classes

log = Log.open_topic ("s-default-nodes")

-- looks for changes in user-preferences and devices added/removed and schedules
-- rescan
SimpleEventHook {
  name = "default-nodes/rescan-trigger",
  interests = {
    EventInterest {
      Constraint { "event.type", "c", "session-item-added", "session-item-removed" },
      Constraint { "event.session-item.interface", "=", "linkable" },
      Constraint { "media.class", "#", "Audio/*" },
    },
    EventInterest {
      Constraint { "event.type", "c", "session-item-added", "session-item-removed" },
      Constraint { "event.session-item.interface", "=", "linkable" },
      Constraint { "media.class", "#", "Video/*" },
    },
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "c", "default.configured.audio.sink",
          "default.configured.audio.source", "default.configured.video.source"
      },
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
    local si_om = source:call ("get-object-manager", "session-item")
    local devices_om = source:call ("get-object-manager", "device")

    log:trace ("re-evaluating default nodes")

    -- Audio Sink
    pushSelectDefaultNodeEvent (source, si_om, devices_om, "audio.sink", "in", {
      "Audio/Sink", "Audio/Duplex"
    })

    -- Audio Source
    pushSelectDefaultNodeEvent (source, si_om, devices_om, "audio.source", "out", {
      "Audio/Source", "Audio/Source/Virtual", "Audio/Duplex", "Audio/Sink"
    })

    -- Video Source
    pushSelectDefaultNodeEvent (source, si_om, devices_om, "video.source", "out", {
      "Video/Source", "Video/Source/Virtual"
    })
  end
}:register ()

function pushSelectDefaultNodeEvent (source, si_om, devices_om, def_node_type,
                                     port_direction, media_classes)
  local nodes =
      collectAvailableNodes (si_om, devices_om, port_direction, media_classes)
  local event = source:call ("create-event", "select-default-node", nil, {
      ["default-node.type"] = def_node_type,
  })
  event:set_data ("available-nodes", Json.Array (nodes))
  EventDispatcher.push_event (event)
end

-- Return an array table where each element is another table containing all the
-- node properties of all the nodes that can be selected for a given media class
-- set and direction
function collectAvailableNodes (si_om, devices_om, port_direction, media_classes)
  local collected = {}

  for linkable in si_om:iterate {
    type = "SiLinkable",
    Constraint { "media.class", "c", table.unpack (media_classes) },
  } do
    local linkable_props = linkable.properties
    local node = linkable:get_associated_proxy ("node")

    -- check that the node has ports in the requested direction
    if not node:lookup_port {
      Constraint { "port.direction", "=", port_direction }
    } then
      goto next_linkable
    end

    -- check that the node has available routes,
    -- if it is associated to a real device
    if not nodeHasAvailableRoutes (node, devices_om) then
      goto next_linkable
    end

    table.insert (collected, Json.Object (node.properties))

    ::next_linkable::
  end

  return collected
end

-- If the node has an associated device, verify that it has an available
-- route. Some UCM profiles expose all paths (headphones, HDMI, etc) as nodes,
-- even though they may not be connected... See #145
function nodeHasAvailableRoutes (node, devices_om)
  local properties = node.properties
  local device_id = properties ["device.id"]
  local cpd = properties ["card.profile.device"]

  if not device_id or not cpd then
    return true
  end

  -- Get the device
  local device = devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" }
  }
  if not device then
    return true
  end

  --  Check if the current device route supports the node card device profile
  for r in device:iterate_params ("Route") do
    local route = r:parse ()
    if route.device == cpd then
      if route.available == "no" then
        return false
      else
        return true
      end
    end
  end

  -- Check if available routes support the node card device profile
  local found = 0
  for r in device:iterate_params ("EnumRoute") do
    local route = r:parse ()
    if type (route.devices) == "table" then
      for _, i in ipairs (route.devices) do
        if i == cpd then
          found = found + 1
          if route.available ~= "no" then
            return true
          end
        end
      end
    end
  end

  -- The node is part of a profile without routes so we assume it
  -- is available. This can happen for Pro Audio profiles
  if found == 0 then
    return true
  end

  return false
end
