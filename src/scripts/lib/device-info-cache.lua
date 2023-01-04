-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

local module = {
  -- table of device info
  dev_infos = {},
}

SimpleEventHook {
  name = "lib/device-info-cache/cleanup",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-removed" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    local device_id = props ["object.serial"]
    Log.trace ("cleaning up dev_info for object.serial = " .. device_id)
    module.dev_infos [device_id] = nil
  end
}:register()

function module.get_device_info (self, device)
  local device_properties = device.properties
  local device_id = device_properties ["object.serial"]
  local dev_info = self.dev_infos [device_id]

  -- new device
  if not dev_info then
    local device_name = device_properties ["device.name"]
    if not device_name then
      Log.critical (device, "invalid device.name")
      return nil
    end

    Log.trace (device, string.format (
        "create dev_info for '%s', object.serial = %s", device_name, device_id))

    dev_info = {
      name = device_name,
      active_profile = -1,
      route_infos = {},
    }
    self.dev_infos [device_id] = dev_info
  end

  return dev_info
end

function module.find_route_info (dev_info, route, return_new)
  local ri = dev_info.route_infos [route.index]
  if not ri and return_new then
    ri = {
      index = route.index,
      name = route.name,
      direction = route.direction,
      devices = route.devices or {},
      profiles = route.profiles,
      priority = route.priority or 0,
      available = route.available or "unknown",
      prev_available = route.available or "unknown",
      active = false,
      prev_active = false,
      save = false,
    }
  end
  return ri
end

return module
