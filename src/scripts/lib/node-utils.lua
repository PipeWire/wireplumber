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

-- object.serial is assigned by PipeWire from a single monotonic counter, so
-- serials of any two objects are comparable and the lower one is the older
-- object. Objects that do not report one sort last and cannot be told apart.
local function get_object_serial (props)
  local serial = props and props ["object.serial"]
  return (serial and math.tointeger (tonumber (serial))) or math.maxinteger
end

-- Builds the key that module.compare_nodes() ranks a node by:
--
--  priority       the session priority, or the driver priority if unset
--  group_serial   the object.serial of the device the node belongs to, or the
--                 node's own if it has none; nodes of the same device share it
--  route_priority the priority of the device route that carries this node
--  serial         the node's own object.serial
function module.get_node_ranking (node_props)
  local serial = get_object_serial (node_props)
  local ranking = {
    priority = module.get_session_priority (node_props),
    group_serial = serial,
    route_priority = 0,
    serial = serial,
  }

  local card_profile_device = node_props ["card.profile.device"]
  local device_id = node_props ["device.id"]

  -- if the node does not have an associated device, it is its own group
  if not card_profile_device or not device_id then
    return ranking
  end

  -- Get the device
  local devices_om = cutils.get_object_manager ("device")
  local device = devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }

  if not device then
    return ranking
  end

  ranking.group_serial = get_object_serial (device.properties)

  -- Get the priority of the associated route
  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if route and (route.device == tonumber (card_profile_device)) then
      ranking.route_priority = route.priority
      break
    end
  end

  return ranking
end

-- Returns true if node ranking 'a' should be preferred over ranking 'b'.
--
-- The device serial outranks the route priority because route priorities rank
-- the outputs of one card against each other and nothing more -- ACP prices
-- analog-output at 9900 and hdmi-output-0 at 5900 on every card that has them
-- -- so only nodes of the same device may be compared on it.
--
-- The node serial makes the outcome independent of the iteration order, which
-- a WpObjectManager reshuffles on every removal. Both serials rank the older
-- object first; between two cards that is the order the PipeWire monitor
-- discovered them in, which is arbitrary, but a tie on priority.session means
-- no preference was expressed.
function module.compare_nodes (a, b)
  if a.priority ~= b.priority then
    return a.priority > b.priority
  elseif a.group_serial ~= b.group_serial then
    return a.group_serial < b.group_serial
  elseif a.route_priority ~= b.route_priority then
    return a.route_priority > b.route_priority
  else
    return a.serial < b.serial
  end
end

function module.has_available_routes (node, devices_om)
  local node_props = node.properties
  local device_id = node_props:get_int ("device.id")
  local card_profile_device = node_props ["card.profile.device"]

  -- Consider routes as available if the node is not associated with any device
  if device_id == nil or card_profile_device == nil then
    return true
  end
  local device = devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }
  if device == nil then
    return true
  end

  -- Check if at least one of the current device routes supports the node's
  -- card profile device and is available
  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if route and (route.device == tonumber (card_profile_device)) then
      if (route.available == "no") then
        return false
      else
        return true
      end
    end
  end

  -- Check if available routes support the node's card profile device
  local found = 0
  for r in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (r, "EnumRoute")
    if route and type (route.devices) == "table" then
      for _, i in ipairs (route.devices) do
        if i == tonumber (card_profile_device) then
          found = found + 1
          if route.available ~= "no" then
            return true
          end
        end
      end
    end
  end

  -- The node is part of a profile without routes so we assume it is available
  if found == 0 then
    return true
  end

  return false
end

return module
