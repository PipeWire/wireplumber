-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- create-item.lua script takes pipewire nodes and creates session items (a.k.a
-- linkable) objects out of them.

cutils = require ("common-utils")
log = Log.open_topic ("s-node")

items = {}

function configProperties (node)
  local properties = node.properties
  local media_class = properties ["media.class"] or ""

  -- ensure a media.type is set
  if not properties ["media.type"] then
    for _, i in ipairs ({ "Audio", "Video", "Midi" }) do
      if media_class:find (i) then
        properties ["media.type"] = i
        break
      end
    end
  end

  properties ["item.node"] = node
  properties ["item.node.direction"] =
      cutils.mediaClassToDirection (media_class)
  properties ["item.node.type"] =
      media_class:find ("^Stream/") and "stream" or "device"
  properties ["item.plugged.usec"] = GLib.get_monotonic_time ()
  properties ["item.features.no-dsp"] =
      Settings.get_boolean ("node.features.audio.no-dsp")
  properties ["item.features.monitor"] =
      Settings.get_boolean ("node.features.audio.monitor-ports")
  properties ["item.features.control-port"] =
      Settings.get_boolean ("node.features.audio.control-port")
  properties ["node.id"] = node ["bound-id"]

  -- set the default media.role, if configured
  -- avoid Settings.get_string(), as it will parse the default "null" value
  -- as a string instead of returning nil
  local default_role = Settings.get ("node.stream.default-media-role")
  if default_role then
    default_role = default_role:parse()
    properties ["media.role"] = properties ["media.role"] or default_role
  end

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
