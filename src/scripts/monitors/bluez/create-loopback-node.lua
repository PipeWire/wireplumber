-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-bluez")

LOOPBACK_SOURCE_ID = 128
DEVICE_SOURCE_ID = 0

function CreateDeviceLoopbackSource (dev_props, dev_id)
  local dev_name = dev_props["api.bluez5.address"] or dev_props["device.name"]
  local dec_desc = dev_props["device.description"] or dev_props["device.name"]
      or dev_props["device.nick"] or dev_props["device.alias"] or "bluetooth-device"
  local target_object = mutils.get_bluez_node_name ("bluez_input",
      dev_props["api.bluez5.address"], dev_props["device.name"], DEVICE_SOURCE_ID)

  -- sanitize description, replace ':' with ' '
  dec_desc = dec_desc:gsub("(:)", " ")

  log:info("create SCO source loopback node: " .. dev_name)

  local args = Json.Object {
    ["capture.props"] = Json.Object {
      ["node.name"] = string.format ("bluez_capture_internal.%s", dev_name),
      ["media.class"] = "Stream/Input/Audio/Internal",
      ["node.description"] =
          string.format ("Bluetooth internal capture stream for %s", dec_desc),
      ["audio.channels"] = 1,
      ["audio.position"] = "[MONO]",
      ["bluez5.loopback"] = true,
      ["stream.dont-remix"] = true,
      ["node.passive"] = true,
      ["node.dont-fallback"] = true,
      ["node.linger"] = true,
      ["state.restore-props"] = false,
      ["target.object"] = target_object,
    },
    ["playback.props"] = Json.Object {
      ["node.name"] = string.format ("bluez_input.%s", dev_name),
      ["node.description"] = string.format ("%s", dec_desc),
      ["node.virtual"] = false,
      ["audio.position"] = "[MONO]",
      ["media.class"] = "Audio/Source",
      ["device.id"] = dev_id,
      ["card.profile.device"] = DEVICE_SOURCE_ID,
      ["device.routes"] = "1",
      ["priority.session"] = 2010,
      ["bluez5.loopback"] = true,
    }
  }
  return LocalModule("libpipewire-module-loopback", args:get_data(), {})
end

SimpleEventHook {
  name = "monitor/bluez/create-loopback-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-bluez-device-loopback-node" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local spa_device = event:get_subject ()

    local device_id = spa_device["bound-id"]
    local props = spa_device.properties
    local device_name = props ["device.name"]

    log:debug (spa_device, "Checking loopbacks on BT device: " .. device_name)

    -- Check if the device supports headset profile
    local has_headset_profile = false
    for p in spa_device:iterate_params("EnumProfile") do
      local profile = cutils.parseParam (p, "EnumProfile")
      if profile.name:find ("headset") then
        has_headset_profile = true
      end
    end

    if has_headset_profile then
      -- Always create the source loopback device if autoswitch is enabled.
      -- Otherwise, only create the source loopback device if the current profile
      -- is headset, and destroy the source loopback deivce if the current profile
      -- is A2DP.
      if Settings.get_boolean ("bluetooth.autoswitch-to-headset-profile") then
        -- Create source loopback
        local source_loopback = spa_device:get_managed_object (LOOPBACK_SOURCE_ID)
        if source_loopback == nil and has_headset_profile then
          source_loopback = CreateDeviceLoopbackSource (props, device_id)
          spa_device:store_managed_object(LOOPBACK_SOURCE_ID, source_loopback)
          log:debug (spa_device, "created loopback source for BT device: " .. device_name)
        end
      else
        -- Check if current profile is headset
        local is_current_profile_headset = false
        for p in spa_device:iterate_params("Profile") do
          local profile = cutils.parseParam (p, "Profile")
          if profile.name:find ("headset") then
            is_current_profile_headset = true
          end
          break
        end

        if is_current_profile_headset then
          -- Create source loopback
          local source_loopback = spa_device:get_managed_object (LOOPBACK_SOURCE_ID)
          if source_loopback == nil and has_headset_profile then
            source_loopback = CreateDeviceLoopbackSource (props, device_id)
            spa_device:store_managed_object(LOOPBACK_SOURCE_ID, source_loopback)
            log:debug (spa_device, "created loopback source for BT device: " .. device_name)
          end
        else
          -- Destroy source loopback
          spa_device:store_managed_object(LOOPBACK_SOURCE_ID, nil)
          log:debug (spa_device, "destroyed loopback source for BT device: " .. device_name)
        end
      end
    end
  end
}:register ()
