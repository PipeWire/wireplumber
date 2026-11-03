-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of common Lua utility functions

local cutils = {}

function cutils.parseBool (var)
  return var and (var:lower () == "true" or var == "1")
end

function cutils.parseParam (param, id)
  local props = param:parse ()
  if props.pod_type == "Object" and props.object_id == id then
    return props.properties
  else
    return nil
  end
end

function cutils.getTargetDirection (properties)
  local target_direction = nil

  if properties ["item.node.direction"] == "output" or
      (properties ["item.node.direction"] == "input" and
          cutils.parseBool (properties ["stream.capture.sink"])) then
    target_direction = "input"
  else
    target_direction = "output"
  end
  return target_direction
end

function cutils.arrayContains (a, value)
  for _, v in ipairs (a) do
    if v == value then
      return true
    end
  end
  return false
end

function cutils.get_application_name ()
  return Core.get_properties()["application.name"] or "WirePlumber"
end

function cutils.get_client_access (client_properties)
  local access = client_properties["pipewire.access"]
  local client_access = client_properties["pipewire.client.access"]
  local is_flatpak = client_properties:get_boolean ("pipewire.sec.flatpak")

  if is_flatpak then
    client_access = "flatpak"
  end

  if client_access == nil then
    return access
  elseif access == "unrestricted" or access == "default" then
    if client_access ~= "unrestricted" then
      return client_access
    end
  end

  return access
end

return cutils
