-- WirePlumber

-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of monitor Lua utility functions

log = Log.open_topic ("s-monitors-utils")

local mutils = {
  cam_data = {},
  cam_source = nil
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

function create_cam_node(cam_data)
  local api = cam_data.properties["device.api"]

  log:info ("create " .. api .. " node for device: " .. cam_data.obj_path)

  source = source or Plugin.find ("standard-event-source")
  local e = source:call ("create-event", "create-" .. api .. "-device-node",
    cam_data.parent, nil)
  e:set_data ("factory", cam_data.factory)
  e:set_data ("node-properties", cam_data.properties)
  e:set_data ("node-sub-id", cam_data.id)

  EventDispatcher.push_event (e)
end

function table_contains(table, element)
  for _, value in pairs(table) do
    if value == element then
      return true
    end
  end
  return false
end

function mutils.create_cam_nodes(self)
  log:debug("create_cam_nodes for " .. #self.cam_data .. " devices")

  local libcamera_cameras = {}
  local v4l2_uvc_cameras = {}
  local v4l2_other_cameras = {}

  for _, data in ipairs(self.cam_data) do
    local api = data.properties["device.api"]

    local dev_ids = data.properties["device.devids"]
    local dev_ids_json = Json.Raw(dev_ids)

    if dev_ids_json:is_array() then
      data.dev_ids = dev_ids_json:parse()
    else
      data.dev_ids = {}
      -- to maintain the backward compatibility with earlier pipewire versions.
      for dev_id_str in dev_ids:gmatch("%S+") do
        local dev_id = tonumber(dev_id_str)
        if dev_id then
          table.insert(data.dev_ids, dev_id)
        end
      end
    end

    if api == 'libcamera' then
      table.insert(libcamera_cameras, data)
    elseif api == 'v4l2' then
      if data.properties["api.v4l2.cap.driver"] == "uvcvideo" then
        table.insert(v4l2_uvc_cameras, data)
      else
        table.insert(v4l2_other_cameras, data)
      end
    else
      log:warning("Got camera with unknown API, ignoring")
    end
  end

  local device_ids_libcamera = {}
  local device_ids_v4l2 = {}

  for _, data in ipairs(v4l2_uvc_cameras) do
    create_cam_node (data)
    table.insert(device_ids_v4l2, data.dev_ids[1])
  end

  for _, data in ipairs(libcamera_cameras) do
    local should_create = true

    for _, dev_id in ipairs(data.dev_ids) do
      if table_contains(device_ids_v4l2, dev_id) then
        should_create = false
        break
      end
    end

    if not should_create then
      log:warning ("skipping device " .. data.obj_path)
    else
      create_cam_node (data)
      for _, dev_id in ipairs(data.dev_ids) do
        table.insert(device_ids_libcamera, dev_id)
      end
    end
  end

  for _, data in ipairs(v4l2_other_cameras) do
    if table_contains(device_ids_libcamera, data.dev_ids[1]) then
      log:debug ("skipping device " .. data.obj_path)
    else
      create_cam_node (data)
    end
  end

  self.cam_data = {}
end


-- arbitrates between v4l2 and libcamera on who gets to create the device node
-- for a device, logic is based on the device number of the device given by both
-- the parties.
function mutils.register_cam_node (self, parent, id, factory, properties)
  local obj_path = properties["object.path"]
  local api = properties["device.api"]
  local dev_ids = properties["device.devids"]
  log:debug(api .. " reported device " .. obj_path .. " with device ids " .. dev_ids)

  -- cache info, it comes handy when creating node
  local cam_data = {}
  cam_data.id = id
  cam_data.parent = parent
  cam_data.obj_path = obj_path
  cam_data.factory = factory
  cam_data.properties = properties

  table.insert(self.cam_data, cam_data)

  if self.cam_source then
    self.cam_source:destroy ()
  end

  self.cam_source = Core.timeout_add (
    Settings.get_int ("monitor.camera-discovery-timeout"), function()
      self:create_cam_nodes ()
      self.cam_source = nil
  end)
end

return mutils
