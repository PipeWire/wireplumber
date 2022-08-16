-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- policy-node.lua is the main policy script, it watches for any changes that
-- can affect the routing(stream to device links) and takes necessary actions.
-- For example: It watches for stream nodes to show up and links them to
-- appropriate target device. It watches for new device preferece of the
-- user(via pavuctrl or gnome settins or metadata etc) and moves the existing
-- sessions to that device(if the device is valid). It watches for new devices
-- to show up(usb headset is plugged in or if BT is paired) and moves the
-- existing streams to that device.

-- settings file: policy.conf

local move = Settings.get ("default-policy-move"):parse() or false
local follow = Settings.get ("default-policy-follow"):parse() or false
local filter_forward_format = Settings.get ("filter.forward-format"):parse() or false

local putils = require ("policy-utils")
local cutils = require ("common-utils")

find_target_events = {}

function parseBool (var)
  return cutils.parseBool (var)
end

function unhandleLinkable (si)
  local si_flags = putils.get_flags (si_id)
  local valid, si_props = checkLinkable (si, true)
  if not valid then
    return
  end

  Log.info (si, string.format ("unhandling item: %s (%s)",
    tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

  -- remove any links associated with this item
  for silink in links_om:iterate () do
    local out_id = tonumber (silink.properties ["out.item.id"])
    local in_id = tonumber (silink.properties ["in.item.id"])
    if out_id == si.id or in_id == si.id then
      if out_id == si.id and
          si_flags and si_flags.peer_id == out_id then
        si_flags.peer_id = nil
      elseif in_id == si.id and
          si_flags and si_flags.peer_id == in_id then
        si_flags.peer_id = nil
      end
      silink:remove ()
      Log.info (silink, "... link removed")
    end
  end

  si_flags = nil
  putils.set_flags (si_id, si_flags)
end

function handleLinkable (si)
  local si_id = si.id;

  local valid, si_props = checkLinkable (si)
  if not valid then
    return
  end

  -- check if we need to link this node at all
  local autoconnect = parseBool (si_props ["node.autoconnect"])
  if not autoconnect then
    Log.debug (si, tostring (si_props ["node.name"]) .. " does not need to be autoconnected")
    return
  end

  if not find_target_events [si_id] then
    find_target_events [si_id] = nil
  end

  if find_target_events [si_id] ~= nil then
    -- stop the processing of the old event, we are going to queue a new one any
    -- way
    find_target_events [si_id]:stop_processing ()
  end

  find_target_events [si_id] = EventDispatcher.push_event {
    type = "find-target-si-and-link", priority = 10, subject = si }
end

function rescan ()
  Log.info ("rescanning..")
  for si in linkables_om:iterate () do
    handleLinkable (si)
  end
end

function checkLinkable (si, handle_nonstreams)
  -- only handle stream session items
  local si_props = si.properties
  if not si_props or (si_props ["item.node.type"] ~= "stream"
      and not handle_nonstreams) then
    return false
  end

  -- Determine if we can handle item by this policy
  if endpoints_om:get_n_objects () > 0 and
      si_props ["item.factory.name"] == "si-audio-adapter" then
    return false
  end

  return true, si_props
end

function findAssociatedLinkGroupNode (si)
  local si_props = si.properties
  local node = si:get_associated_proxy ("node")
  local link_group = node.properties ["node.link-group"]
  if link_group == nil then
    return nil
  end

  -- get the associated media class
  local assoc_direction = cutils.getTargetDirection (si_props)
  local assoc_media_class =
  si_props ["media.type"] ..
      (assoc_direction == "input" and "/Sink" or "/Source")

  -- find the linkable with same link group and matching assoc media class
  for assoc_si in linkables_om:iterate () do
    local assoc_node = assoc_si:get_associated_proxy ("node")
    local assoc_link_group = assoc_node.properties ["node.link-group"]
    if assoc_link_group == link_group and
        assoc_media_class == assoc_node.properties ["media.class"] then
      return assoc_si
    end
  end

  return nil
end

function onLinkGroupPortsStateChanged (si, old_state, new_state)
  local new_str = tostring (new_state)
  local si_props = si.properties

  -- only handle items with configured ports state
  if new_str ~= "configured" then
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

function checkFiltersPortsState (si)
  local si_props = si.properties
  local node = si:get_associated_proxy ("node")
  local link_group = node.properties ["node.link-group"]
  local si_id = si.id
  local si_flags = putils.get_flags (si_id)

  -- only listen for ports state changed on audio filter streams
  if si_flags.ports_state_signal ~= true and
      si_props ["item.factory.name"] == "si-audio-adapter" and
      si_props ["item.node.type"] == "stream" and
      link_group ~= nil then
    si:connect ("adapter-ports-state-changed", onLinkGroupPortsStateChanged)
    si_flags.ports_state_signal = true
    putils.set_flags (si_id, si_flags)
    Log.info (si, "listening ports state changed on " .. si_props ["node.name"])
  end
end

SimpleEventHook {
  name = "handle-linkable@policy-node",
  type = "on-event",
  priority = "linkable-added-create-item",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "object-added" },
      Constraint { "event.subject.type", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "media.class", "#", "Stream/*", type = "pw-global" },
      Constraint { "active-features", "!", 0, type = "gobject" },
    },
  },
  execute = function (event)
    local si = event:get_subject ()
    local si_props = si.properties

    -- Forward filters ports format to associated virtual devices if enabled
    if filter_forward_format then
      checkFiltersPortsState (si)
    end

    handleLinkable (si)
  end
}:register ()

SimpleEventHook {
  name = "linkable-removed@policy-node",
  type = "on-event",
  priority = "linkable-removed-create-item",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "object-removed" },
      Constraint { "event.subject.type", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "media.class", "#", "Stream/*", type = "pw-global" },
      Constraint { "active-features", "!", 0, type = "gobject" },
    },
  },
  execute = function (event)
    local si = event:get_subject ()
    unhandleLinkable (si)
  end
}:register ()

SimpleEventHook {
  name = "rescan-policy",
  priority = "rescan-policy",
  type = "after-events",
  interests = {
    -- on audio device node linkable addition
    EventInterest {
      Constraint { "event.type", "=", "object-added" },
      Constraint { "event.subject.type", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "media.class", "#", "Audio/*", type = "pw-global" },
      Constraint { "active-features", "!", 0, type = "gobject" },
    },
    -- on video device node linkable addition
    EventInterest {
      Constraint { "event.type", "=", "object-added" },
      Constraint { "event.subject.type", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "media.class", "#", "Video/*", type = "pw-global" },
      Constraint { "active-features", "!", 0, type = "gobject" },
    },
    -- on audio device node linkable removal
    EventInterest {
      Constraint { "event.type", "=", "object-removed" },
      Constraint { "event.subject.type", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "media.class", "#", "Audio/*", type = "pw-global" },
      Constraint { "active-features", "!", 0, type = "gobject" },
    },
    -- on video device node linkable removal
    EventInterest {
      Constraint { "event.type", "=", "object-removed" },
      Constraint { "event.subject.type", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "media.class", "#", "Video/*", type = "pw-global" },
      Constraint { "active-features", "!", 0, type = "gobject" },
    },
    -- on device Routes changed
    EventInterest {
      Constraint { "event.type", "=", "params-changed" },
      Constraint { "event.subject.type", "=", "device" },
      Constraint { "event.subject.param-id", "=", "Route" },
    },
  },
  execute = function ()
    rescan ()
  end
}:register ()

if follow then
  SimpleEventHook {
    name = "follow@policy-node",
    type = "after-events",
    priority = "rescan-policy",
    interests = {
      EventInterest {
        Constraint { "event.type", "=", "object-changed" },
        Constraint { "event.subject.type", "=", "metadata" },
        Constraint { "metadata.name", "=", "default" },
        Constraint { "event.subject.key", "=", "default.audio.source" },
      },
      EventInterest {
        Constraint { "event.type", "=", "object-changed" },
        Constraint { "event.subject.type", "=", "metadata" },
        Constraint { "metadata.name", "=", "default" },
        Constraint { "event.subject.key", "=", "default.audio.sink" },
      },
      EventInterest {
        Constraint { "event.type", "=", "object-changed" },
        Constraint { "event.subject.type", "=", "metadata" },
        Constraint { "metadata.name", "=", "default" },
        Constraint { "event.subject.key", "=", "default.video.source" },
      },
    },
    execute = function ()
      rescan ()
    end
  }:register ()
end

if move then
  SimpleEventHook {
    name = "move@policy-node",
    type = "after-events",
    priority = "rescan-policy",
    interests = {
      EventInterest {
        Constraint { "event.type", "=", "object-changed" },
        Constraint { "event.subject.type", "=", "metadata" },
        Constraint { "metadata.name", "=", "default" },
        Constraint { "event.subject.key", "=", "target.node" },
      },
      EventInterest {
        Constraint { "event.type", "=", "object-changed" },
        Constraint { "event.subject.type", "=", "metadata" },
        Constraint { "metadata.name", "=", "default" },
        Constraint { "event.subject.key", "=", "target.object" },
      },
    },
    execute = function ()
      rescan ()
    end
  }:register ()
end

default_nodes = Plugin.find ("default-nodes-api")

endpoints_om = ObjectManager { Interest { type = "SiEndpoint" } }

linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    -- only handle si-audio-adapter and si-node
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "active-features", "!", 0, type = "gobject" },
  }
}

pending_linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    -- only handle si-audio-adapter and si-node
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "active-features", "=", 0, type = "gobject" },
  }
}

endpoints_om:activate ()
linkables_om:activate ()
pending_linkables_om:activate ()
