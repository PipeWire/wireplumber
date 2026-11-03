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
  local factory_name = properties ["factory.name"] or ""

  -- ensure a media.type is set
  if not properties ["media.type"] then
    properties ["media.type"] = "Unknown"
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
  properties ["item.features.mono"] =
      (factory_name == "api.alsa.pcm.sink" or factory_name == "api.bluez5.a2dp.sink") and
      Settings.get_boolean ("node.features.audio.mono")
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

AsyncEventHook {
  name = "node/reconfigure-audio-adapters",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "setting-changed" },
      Constraint { "event.subject.key", "#", "node.features.audio.*" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local reconfigured = {}

        -- Remove and re-configure all audio adapters, so that they are unlinked
        for id, item in pairs (items) do
          if item.properties ["item.factory.name"] == "si-audio-adapter" then
            log:info (item, "Started re-configuring audio adapter")
            item:remove ()
            if item:configure (configProperties (
                item:get_associated_proxy ("node"))) then
              reconfigured [id] = item
            else
              log:warning (item, "Could not re-configure audio adapter")
              items [id] = nil
            end
          end
        end

        -- Register the items only after all of them are activated, so that
        -- streams are not linked before their target devices are available
        local pending = 0
        for _ in pairs (reconfigured) do
          pending = pending + 1
        end
        if pending == 0 then
          transition:advance ()
          return
        end

        for id, item in pairs (reconfigured) do
          item:activate (Features.ALL, function (si, e)
            if e then
              log:warning (si, "Could not re-activate audio adapter")
              reconfigured [id] = nil
            else
              log:info (si, "Successfully re-activated audio adapter")
            end

            pending = pending - 1
            if pending == 0 then
              for rid, ritem in pairs (reconfigured) do
                -- skip items whose node was removed in the meantime
                if items [rid] == ritem then
                  ritem:register ()
                end
              end
              transition:advance ()
            end
          end)
        end
      end,
    },
  },
}:register ()
