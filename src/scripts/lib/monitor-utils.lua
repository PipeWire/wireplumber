-- WirePlumber

-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of monitor Lua utility functions

log = Log.open_topic ("s-monitors-utils")

local mutils = {
  cam_data = {}
}

-- finds out if any of the managed objects(nodes of a device or devices of
-- device enumerator) has duplicate values
function mutils.find_duplicate (parent, id, property, value)
  for i = 0, id - 1, 1 do
    local obj = parent:get_managed_object (i)
    if obj and obj.properties[property] == value then
      return true
    end
  end
  return false
end

function mutils.get_cam_data (self, dev_string)
  local dev_num = tonumber (dev_string)
  if not dev_num then
    return
  end

  if not self.cam_data[dev_num] then
    self.cam_data[dev_num] = {}
    self.cam_data[dev_num]["libcamera"] = {}
    self.cam_data[dev_num]["v4l2"] = {}
  end

  return self.cam_data[dev_num], dev_num
end

function mutils.clear_cam_data (self, dev_string)
  local dev_num = tonumber (dev_string)
  if not dev_num then
    return
  end

  self.cam_data[dev_num] = nil
end

function mutils.create_cam_node (self, dev_num)
  local api = nil
  local cam_data = self:get_cam_data (dev_num)

  if cam_data["v4l2"].enum_status and cam_data["libcamera"].enum_status then
    if cam_data.is_device_uvc then
      api = "v4l2"
    else
      api = "libcamera"
    end
  else
    api = cam_data["v4l2"].enum_status and "v4l2" or "libcamera"
  end

  log:info (string.format ("create \"%s\" node for device:%s%s", api,
    cam_data.dev_path, (cam_data.is_device_uvc and "(uvc)" or "")))

  source = source or Plugin.find ("standard-event-source")
  local e = source:call ("create-event", "create-" .. api .. "-device-node",
    cam_data[api].parent, nil)
  e:set_data ("factory", cam_data[api].factory)
  e:set_data ("node-properties", cam_data[api].properties)
  e:set_data ("node-sub-id", cam_data[api].id)

  EventDispatcher.push_event (e)

  self:clear_cam_data (dev_num)
end

-- arbitrates between v4l2 and libcamera on who gets to create the device node
-- for a device, logic is based on the device number of the device given by both
-- the parties.
function mutils.register_cam_node (self, parent, id, factory, properties)
  local cam_data, dev_num = self:get_cam_data (properties["device.devids"])
  local api = properties["device.api"]

  if not cam_data then
    log:notice (string.format ("device number invalid for %s device:%s",
      api, properties["device.name"]))
    return false
  end

  -- only v4l2 can give this info
  if properties["api.v4l2.cap.driver"] == "uvcvideo" then
    cam_data.is_device_uvc = true
  end

  -- only v4l2 can give this info
  if properties["api.v4l2.path"] then
    cam_data.dev_path = properties["api.v4l2.path"]
  end

  cam_api_data = cam_data[api]
  cam_api_data.enum_status = true

  -- cache info, it comes handy when creating node
  cam_api_data.parent = parent
  cam_api_data.id = id
  cam_api_data.factory = factory
  cam_api_data.properties = properties

  local other_api = api == "v4l2" and "libcamera" or "v4l2"
  if cam_api_data.enum_status and not cam_data[other_api].enum_status then
    log:trace (string.format ("\"%s\" armed a timer for %d", api, dev_num))
    cam_data.source = Core.timeout_add (
        Settings.get_int ("monitor.camera-discovery-timeout"), function()
      log:trace (string.format ("\"%s\" armed timer expired for %d", api, dev_num))
      self:create_cam_node (dev_num)
      cam_data.source = nil
    end)
  elseif cam_data.source then
    log:trace (string.format ("\"%s\" disarmed timer for %d", api, dev_num))
    cam_data.source:destroy ()
    cam_data.source = nil
    self:create_cam_node (dev_num)
  else
    log:notice (string.format ("\"%s\" calling after timer expiry for %d:%s%s",
      api, dev_num, cam_data.dev_path,
      (cam_data.is_device_uvc and "(uvc)" or "")))
  end

  return true
end

return mutils
