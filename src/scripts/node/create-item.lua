-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- create-item.lua script takes pipewire nodes and creates session items (a.k.a
-- linkable) objects out of them.

cutils = require ("common-utils")
settings = require ("settings-node")
log = Log.open_topic ("s-node")

items = {}

function configProperties (node)
  local np = node.properties
  local properties = {
    ["item.node"] = node,
    ["item.plugged.usec"] = GLib.get_monotonic_time (),
    ["item.features.no-dsp"] = settings ["features.audio.no-dsp"],
    ["item.features.monitor"] = settings ["features.audio.monitor-ports"],
    ["item.features.control-port"] = settings ["features.audio.control-port"],
    ["node.id"] = node ["bound-id"],
    ["client.id"] = np ["client.id"],
    ["object.path"] = np ["object.path"],
    ["object.serial"] = np ["object.serial"],
    ["target.object"] = np ["target.object"],
    ["priority.session"] = np ["priority.session"],
    ["device.id"] = np ["device.id"],
    ["card.profile.device"] = np ["card.profile.device"],
    ["node.dont-fallback"] = np ["node.dont-fallback"],
    ["node.dont-move"] = np ["node.dont-move"],
    ["node.linger"] = np ["node.linger"],
  }

  for k, v in pairs (np) do
    if k:find ("^node") or k:find ("^stream") or k:find ("^media") then
      properties [k] = v
    end
  end

  local media_class = properties ["media.class"] or ""

  if not properties ["media.type"] then
    for _, i in ipairs ({ "Audio", "Video", "Midi" }) do
      if media_class:find (i) then
        properties ["media.type"] = i
        break
      end
    end
  end

  properties ["item.node.type"] =
      media_class:find ("^Stream/") and "stream" or "device"

  properties ["item.node.direction"] = cutils.mediaClassToDirection (media_class)
  return properties
end

AsyncEventHook {
  name = "node/create-item",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "#", "Stream/*", type = "pw-global" },
    },
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "#", "Video/*", type = "pw-global" },
    },
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "#", "Audio/*", type = "pw-global" },
      Constraint { "wireplumber.is-virtual", "-", type = "pw" },
    },
  },
  steps = {
    start = {
      next = "register",
      execute = function (event, transition)
        local node = event:get_subject ()
        local id = node.id
        local item
        local item_type

        local media_class = node.properties ['media.class']
        if string.find (media_class, "Audio") then
          item_type = "si-audio-adapter"
        else
          item_type = "si-node"
        end

        log:info (node, "creating item for node -> " .. item_type)

        -- create item
        item = SessionItem (item_type)
        items [id] = item

        -- configure item
        if not item:configure (configProperties (node)) then
          transition:return_error ("failed to configure item for node "
              .. tostring (id))
          return
        end

        -- activate item
        item:activate (Features.ALL, function (_, e)
          if e then
            transition:return_error ("failed to activate item: "
                .. tostring (e));
          else
            transition:advance ()
          end
        end)
      end,
    },
    register = {
      next = "none",
      execute = function (event, transition)
        local node = event:get_subject ()
        local bound_id = node ["bound-id"]
        local item = items [node.id]

        log:info (item, "activated item for node " .. tostring (bound_id))
        item:register ()
        transition:advance ()
      end,
    },
  },
}:register ()

SimpleEventHook {
  name = "node/destroy-item",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-removed" },
      Constraint { "media.class", "#", "Stream/*", type = "pw-global" },
    },
    EventInterest {
      Constraint { "event.type", "=", "node-removed" },
      Constraint { "media.class", "#", "Video/*", type = "pw-global" },
    },
    EventInterest {
      Constraint { "event.type", "=", "node-removed" },
      Constraint { "media.class", "#", "Audio/*", type = "pw-global" },
      Constraint { "wireplumber.is-virtual", "-", type = "pw" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local id = node.id
    if items [id] then
      items [id]:remove ()
      items [id] = nil
    end

  end
}:register ()
