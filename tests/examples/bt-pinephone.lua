#!/usr/bin/wpexec
--
-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--    @author Frédéric Danis <frederic.danis@collabora.com>
--
-- SPDX-License-Identifier: MIT
--
-- This is an example of the offload SCO nodes platform specific management,
-- in this case for the PinePhone.
--
-- The PinePhone provides specific ALSA ports to route audio to the Bluetooth
-- chipset. This script selects these ports when the offload SCO nodes state
-- change to 'running'.
--
-- This scriptcan be executed as a standalone executable, or it can be placed
-- in WirePlumber's scripts directory and loaded together with other scripts.
-----------------------------------------------------------------------------

devices_om = ObjectManager {
  Interest {
    type = "device",
  }
}

nodes_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "node.name", "#", "*.bluez_*put*"},
    Constraint { "device.id", "+" },
  }
}

function parseParam(param, id)
  local route = param:parse()
  if route.pod_type == "Object" and route.object_id == id then
    return route.properties
  else
    return nil
  end
end

function setPlatformRoute (route_name, direction)
  local platform_sound = nil
  local device_id = nil
  local route_save = nil

  local interest = Interest {
    type = "device",
    Constraint { "device.name", "=", "alsa_card.platform-sound"}
  }
  for d in devices_om:iterate (interest) do
    platform_sound = d

    for p in platform_sound:iterate_params("Route") do
      route = parseParam(p, "Route")
      if route.direction == direction then
        device_id = route.device
        route_save = route.save
      end
    end

    for p in platform_sound:iterate_params("EnumRoute") do
      enum_route = parseParam(p, "EnumRoute")

      if enum_route.name == route_name then
        route_index = enum_route.index
      end
    end
  end

  if route.index == route_index then
    return
  end

  -- default props
  local props = {
    "Spa:Pod:Object:Param:Props", "Route",
    mute = false,
  }

  -- construct Route param
  local param = Pod.Object {
    "Spa:Pod:Object:Param:Route", "Route",
    index = route_index,
    device = device_id,
    props = Pod.Object(props),
    save = route_save,
  }

  Log.info(param, "setting route on " .. tostring(platform_sound))
  platform_sound:set_param("Route", param)

  route.prev_active = true
  route.active = true
end

nodes_om:connect("object-added", function(_, node)
  node:connect("state-changed", function(node, old_state, cur_state)
    local interest = Interest {
      type = "device",
      Constraint { "object.id", "=", node.properties["device.id"]}
    }
    for d in devices_om:iterate (interest) do
      if cur_state == "running" then
        -- Both direction should be set to allow audio streaming
        setPlatformRoute("[Out] BluetoothHeadset", "Output")
        setPlatformRoute("[In] BluetoothHeadset", "Input")
      else
        setPlatformRoute("[Out] Earpiece", "Output")
        setPlatformRoute("[In] DigitalMic", "Input")
      end
    end
  end)
end)

nodes_om:activate()
devices_om:activate()
