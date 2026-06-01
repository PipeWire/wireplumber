-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-monitors-bluez")

AsyncEventHook {
  name = "monitor/bluez/create-device",
  after = "monitor/bluez/name-device",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-bluez-device" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function(event, transition)
        local source = event:get_source ()
        local properties = event:get_data ("device-properties")
        local factory = event:get_data ("factory")
        local monitor = event:get_subject ()
        local id = event:get_data ("device-sub-id")

        log:info (monitor, "Handling device " .. properties["device.name"])

        -- Don't do anything if this device is disabled
        if properties:get_boolean ("device.disabled") then
          log:notice ("Bluez device " .. properties["device.name"] .. " disabled")
          transition:advance ()
          event:stop_processing ()
          return
        end

        -- Create the BT device
        local device = SpaDevice (factory, properties)
        if device == nil then
          log:warning ("Failed to create '" .. factory .. "' device")
          transition:advance ()
          event:stop_processing ()
          return
        end

        -- Handle create-object signal
        device:connect ("create-object", function (parent, id, type, factory, properties)
          local e = source:call ("create-event", "create-bluez-device-node", parent, nil)
          e:set_data ("node-properties", properties)
          e:set_data ("type", type)
          e:set_data ("factory", factory)
          e:set_data ("node-sub-id", id)
          EventDispatcher.push_event (e)
        end)

        -- Handle object-removed signal
        device:connect ("object-removed", function (parent, id)
          local e = source:call ("create-event", "remove-bluez-device-node", parent, nil)
          e:set_data ("node-sub-id", id)
          EventDispatcher.push_event (e)
        end)

        -- Handle params-changed signal
        device:connect ("params-changed", function (spa_device, param_name)
          if param_name == "Profile" or param_name == "EnumProfile" then
            local e = source:call ("create-event", "create-bluez-device-loopback-node", spa_device, nil)
            EventDispatcher.push_event (e)
          end
        end)

        log:info (monitor, string.format("%d, %s (%s): %s",
            id, properties["device.description"],
            properties["api.bluez5.address"],
            properties["api.bluez5.connection"]))

        -- Deactivate the device and stop processing if connection is not 'connected'
        if properties["api.bluez5.connection"] ~= "connected" then
          log:info (monitor, "Not activating device " .. properties["device.name"])
          device:deactivate(Features.ALL)
          transition:advance ()
          event:stop_processing ()
          return
        end

        -- Activate the device
        device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND, function (d, e)
          if e ~= nil then
            transition:return_error ("Failed to activate SPA device " ..
                d:get_property ("device.name") .. ": " .. e)
            event:stop_processing ()
            return
          end

          log:info (monitor, "Activated SPA device " .. properties["device.name"])

          -- Store the device as managed object in the monitor
          monitor:store_managed_object (id, device)

          -- Push event to create loopbacks if needed*/
          local e = source:call ("create-event", "create-bluez-device-loopback-node", device, nil)
          EventDispatcher.push_event (e)

          -- Advance
          transition:advance ()
        end)
      end
    }
  }
}:register ()
