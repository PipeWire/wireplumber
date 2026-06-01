-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-monitors-bluez")

config = {}
config.rules = Conf.get_section_as_json ("monitor.bluez.rules", Json.Array {})

SimpleEventHook {
  name = "monitor/bluez/name-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-bluez-device" },
    },
  },
  execute = function(event)
    local properties = event:get_data ("device-properties")
    local parent = event:get_subject ()
    local id = event:get_data ("device-sub-id")

    log:info (parent, "Handling device " .. tostring (id))

    -- ensure a proper device name
    local name =
        (properties["device.name"] or
         properties["api.bluez5.address"] or
         properties["device.description"] or
         tostring(id)):gsub("([^%w_%-%.])", "_")
    if not name:find("^bluez_card%.", 1) then
      name = "bluez_card." .. name
    end
    properties["device.name"] = name

    -- set the icon name
    if not properties["device.icon-name"] then
      local icon = nil
      local icon_map = {
        -- form factor -> icon
        ["microphone"] = "audio-input-microphone",
        ["webcam"] = "camera-web",
        ["handset"] = "phone",
        ["portable"] = "multimedia-player",
        ["tv"] = "video-display",
        ["headset"] = "audio-headset",
        ["headphone"] = "audio-headphones",
        ["speaker"] = "audio-speakers",
        ["hands-free"] = "audio-handsfree",
      }
      local f = properties["device.form-factor"]
      local b = properties["device.bus"]

      icon = icon_map[f] or "audio-card"
      properties["device.icon-name"] = icon .. (b and ("-" .. b) or "")
    end

    -- initial profile is to be set by policy-device-profile.lua, not spa-bluez5
    properties["bluez5.profile"] = "off"
    properties["spa.object.id"] = id

    -- apply properties from rules defined in JSON .conf file
    properties = JsonUtils.match_rules_update_properties (config.rules, properties)

    event:set_data ("device-properties", properties)
  end
}:register ()
