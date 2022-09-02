-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- create-item.lua script takes pipewire nodes and creates session items (a.k.a
-- linkable) objects out of them.

items = {}

function configProperties (node)
  local np = node.properties
  local properties = {
    ["item.node"] = node,
    ["item.plugged.usec"] = GLib.get_monotonic_time (),
    ["item.features.no-dsp"] =
        Settings.parse_boolean_safe ("policy.default.audio-no-dsp", false),
    ["item.features.monitor"] = true,
    ["item.features.control-port"] = false,
    ["node.id"] = node ["bound-id"],
    ["client.id"] = np ["client.id"],
    ["object.path"] = np ["object.path"],
    ["object.serial"] = np ["object.serial"],
    ["target.object"] = np ["target.object"],
    ["priority.session"] = np ["priority.session"],
    ["device.id"] = np ["device.id"],
    ["card.profile.device"] = np ["card.profile.device"],
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

  if media_class:find ("Sink") or
      media_class:find ("Input") or
      media_class:find ("Duplex") then
    properties ["item.node.direction"] = "input"
  elseif media_class:find ("Source") or media_class:find ("Output") then
    properties ["item.node.direction"] = "output"
  end
  return properties
end

AsyncEventHook {
  name = "node-added@create-item",
  type = "on-event",
  priority = "node-added-create-item",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "object-added" },
      Constraint { "event.subject.type", "=", "node" },
      Constraint { "media.class", "#", "Stream/*", type = "pw-global" },
    },
    EventInterest {
      Constraint { "event.type", "=", "object-added" },
      Constraint { "event.subject.type", "=", "node" },
      Constraint { "media.class", "#", "Video/*", type = "pw-global" },
    },
    EventInterest {
      Constraint { "event.type", "=", "object-added" },
      Constraint { "event.subject.type", "=", "node" },
      Constraint { "media.class", "#", "Audio/*", type = "pw-global" },
      Constraint { "wireplumber.is-endpoint", "-", type = "pw" },
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

        Log.info (item, "activated item for node " .. tostring (bound_id))
        item:register ()
        transition:advance ()
      end,
    },
  },
}:register ()

SimpleEventHook {
  name = "node-removed@create-item",
  type = "on-event",
  priority = "node-removed-create-item",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "object-removed" },
      Constraint { "event.subject.type", "=", "node" },
      Constraint { "media.class", "#", "Stream/*", type = "pw-global" },
    },
    EventInterest {
      Constraint { "event.type", "=", "object-removed" },
      Constraint { "event.subject.type", "=", "node" },
      Constraint { "media.class", "#", "Video/*", type = "pw-global" },
    },
    EventInterest {
      Constraint { "event.type", "=", "object-removed" },
      Constraint { "event.subject.type", "=", "node" },
      Constraint { "media.class", "#", "Audio/*", type = "pw-global" },
      Constraint { "wireplumber.is-endpoint", "-", type = "pw" },
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
