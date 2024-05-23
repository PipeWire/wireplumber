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

function get_cam_data(self, dev_id)
  if not self.cam_data[dev_id] then
    self.cam_data[dev_id] = {}
    self.cam_data[dev_id]["libcamera"] = {}
    self.cam_data[dev_id]["v4l2"] = {}
  end
  return self.cam_data[dev_id]
end

function parse_devids_get_cam_data(self, devids)
  local dev_ids_json = Json.Raw(devids)
  local dev_ids_table = {}

  if dev_ids_json:is_array() then
    dev_ids_table = dev_ids_json:parse()
  else
    -- to maintain the backward compatibility with earlier pipewire versions.
    for dev_id_str in devids:gmatch("%S+") do
      local dev_id = tonumber(dev_id_str)
      if dev_id then
        table.insert(dev_ids_table, dev_id)
      end
    end
  end

  local dev_num = nil
  -- `device.devids` is a json array of device numbers
  for _, dev_id_str in ipairs(dev_ids_table) do
    local dev_id = tonumber(dev_id_str)
    if not dev_id then
      log:notice ("invalid device number")
      return
    end

    log:debug ("Working on device " .. dev_id)
    local dev_cam_data = get_cam_data (self, dev_id)
    if not dev_num then
      dev_num = dev_id
      if #dev_ids_table > 1 then
        -- libcam node can some times use more tha one V4L2 devices, in this
        -- case, return the first device id and mark rest of the them as peers
        -- to the first one.
        log:debug ("Device " .. dev_id .. " uses multi V4L2 devices")
        dev_cam_data.uses_multi_v4l2_devices = true
      end
    else
      log:debug ("Device " .. dev_id .. " is peer to " .. dev_num)
      dev_cam_data.peer_id = dev_num
    end
  end

  if dev_num then
    return self.cam_data[dev_num], dev_num
  end
end

function mutils.clear_cam_data (self, dev_num)
  local dev_cam_data = self.cam_data[dev_num]
  if not dev_num then
    return
  end

  if dev_cam_data.uses_multi_v4l2_devices then
    for dev_id, cam_data_ in pairs(self.cam_data) do
      if cam_data_.peer_id == dev_num then
        log:debug("clear " .. dev_id .. " it is peer to " .. dev_num)
        self.cam_data[dev_id] = nil
      end
    end
  end

  self.cam_data[dev_num] = nil
end

function mutils.create_cam_node(self, dev_num)
  local api = nil
  local cam_data = get_cam_data (self, dev_num)

  if cam_data["v4l2"].enum_status and cam_data["libcamera"].enum_status then
    if cam_data.uses_multi_v4l2_devices then
      api = "libcamera"
    elseif cam_data.peer_id ~= nil then
      -- no need to create node for peer
      log:notice ("timer expired for peer device " .. dev_num)
      return
    elseif cam_data.is_device_uvc then
      api = "v4l2"
    else
      api = "libcamera"
    end
  else
    api = cam_data["v4l2"].enum_status and "v4l2" or "libcamera"
  end

  log:info (string.format ("create \"%s\" node for device:%s", api,
    cam_data.dev_path))

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
  local api = properties["device.api"]
  local dev_ids = properties["device.devids"]
  log:debug(api .. " reported " .. dev_ids)

  local cam_data, dev_num = parse_devids_get_cam_data(self, dev_ids)

  if not cam_data then
    log:notice (string.format ("device numbers invalid for %s device:%s",
      api, properties["device.name"]))
    return false
  end

  -- only v4l2 can give this info
  if properties["api.v4l2.cap.driver"] == "uvcvideo" then
    log:debug ("Device " .. dev_num .. " is a UVC device")
    cam_data.is_device_uvc = true
  end

  -- only v4l2 can give this info
  if properties["api.v4l2.path"] then
    cam_data.dev_path = properties["api.v4l2.path"]
  end

  local cam_api_data = cam_data[api]
  cam_api_data.enum_status = true

  -- cache info, it comes handy when creating node
  cam_api_data.parent = parent
  cam_api_data.id = id
  cam_api_data.name = properties["device.name"]
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
