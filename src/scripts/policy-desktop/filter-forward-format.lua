-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Logic to "forward" the format set on special filter nodes to their
-- virtual device peer node. This is for things like the "loopback" module,
-- which always comes in pairs of 2 nodes, one stream and one virtual device.
--
-- FIXME: this script can be further improved

local putils = require ("policy-utils")
local config = require ("policy-config")

function findAssociatedLinkGroupNode (si)
  local si_props = si.properties
  local link_group = si_props ["node.link-group"]
  if link_group == nil then
    return nil
  end

  local std_event_source = Plugin.find ("standard-event-source")
  local om = std_event_source:call ("get-object-manager", "session-item")

  -- get the associated media class
  local assoc_direction = cutils.getTargetDirection (si_props)
  local assoc_media_class = si_props ["media.type"] ..
      (assoc_direction == "input" and "/Sink" or "/Source")

  -- find the linkable with same link group and matching assoc media class
  for assoc_si in om:iterate { type = "SiLinkable" } do
    local assoc_props = assoc_si.properties
    local assoc_link_group = assoc_props ["node.link-group"]
    if assoc_link_group == link_group and
        assoc_media_class == assoc_props ["media.class"] then
      return assoc_si
    end
  end

  return nil
end

function onLinkGroupPortsStateChanged (si, old_state, new_state)
  local si_props = si.properties

  -- only handle items with configured ports state
  if new_state ~= "configured" then
    return
  end

  Log.info (si, "ports format changed on " .. si_props ["node.name"])

  -- find associated device
  local si_device = findAssociatedLinkGroupNode (si)
  if si_device ~= nil then
    local device_node_name = si_device.properties ["node.name"]

    -- get the stream format
    local f, m = si:get_ports_format ()

    -- unregister the device
    Log.info (si_device, "unregistering " .. device_node_name)
    si_device:remove ()

    -- set new format in the device
    Log.info (si_device, "setting new format in " .. device_node_name)
    si_device:set_ports_format (f, m, function (item, e)
      if e ~= nil then
        Log.warning (item, "failed to configure ports in " ..
          device_node_name .. ": " .. e)
      end

      -- register back the device
      Log.info (item, "registering " .. device_node_name)
      item:register ()
    end)
  end
end

SimpleEventHook {
  name = "filter-forward-format@policy-node",
  priority = HookPriority.NORMAL,
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "session-item-added" },
      Constraint { "event.session-item.interface", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "media.class", "#", "Stream/*", type = "pw-global" },
      Constraint { "active-features", "!", 0, type = "gobject" },
    },
  },
  execute = function (event)
    local si = event:get_subject ()

    -- Forward filters ports format to associated virtual devices if enabled
    if config.filter_forward_format then
      local si_props = si.properties
      local link_group = si_props ["node.link-group"]
      local si_flags = putils:get_flags (si.id)

      -- only listen for ports state changed on audio filter streams
      if si_flags.ports_state_signal ~= true and
          si_props ["item.factory.name"] == "si-audio-adapter" and
          si_props ["item.node.type"] == "stream" and
          link_group ~= nil then
        si:connect ("adapter-ports-state-changed", onLinkGroupPortsStateChanged)
        si_flags.ports_state_signal = true
        Log.info (si, "listening ports state changed on " .. si_props ["node.name"])
      end
    end
  end
}:register ()
