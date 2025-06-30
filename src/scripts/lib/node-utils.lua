-- WirePlumber
--
-- Copyright © 2024 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

local cutils = require ("common-utils")

local module = {}

function module.get_session_priority (node_props)
  local priority = node_props ["priority.session"]
  -- fallback to driver priority if session priority is not set
  if not priority then
    priority = node_props ["priority.driver"]
  end
  return math.tointeger (priority) or 0
end

function module.get_route_priority (node_props)
  local card_profile_device = node_props ["card.profile.device"]
  local device_id = node_props ["device.id"]

  -- if the node does not have an associated device, just return 0
  if not card_profile_device or not device_id then
    return 0
  end

  -- Get the device
  devices_om = cutils.get_object_manager ("device")
  local device = devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }

  if not device then
    return 0
  end

  -- Get the priority of the associated route
  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if route and (route.device == tonumber (card_profile_device)) then
      return route.priority
    end
  end

  return 0
end

return module
