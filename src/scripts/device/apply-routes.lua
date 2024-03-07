-- WirePlumber
--
-- Copyright © 2021-2022 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- Based on default-routes.c from pipewire-media-session
-- Copyright © 2020 Wim Taymans
--
-- SPDX-License-Identifier: MIT
--
-- Set the Route param as part of the "select-routes" event run

devinfo = require ("device-info-cache")
log = Log.open_topic ("s-device")

AsyncEventHook {
  name = "device/apply-routes",
  after = { "device/find-stored-routes",
            "device/find-best-routes",
            "device/apply-route-props" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-routes" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local device = event:get_subject ()
        local selected_routes = event:get_data ("selected-routes")

        local dev_info = devinfo:get_device_info (device)
        assert (dev_info)

        if not selected_routes then
          log:info (device, "No routes selected to set on " .. dev_info.name)
          transition:advance ()
          return
        end

        for device_id, route in pairs (selected_routes) do
           -- JSON to lua table
          route = Json.Raw (route):parse ()

           -- steal the props
          local props = route.props or {}

          -- replace with the full route info
          local route_info = devinfo.find_route_info (dev_info, route)
          if not route_info then
            goto skip_route
          end

          -- ensure default values
          local is_input = (route_info.direction == "Input")
          props.mute = props.mute or false
          props.channelVolumes = props.channelVolumes or
              { is_input and Settings.get_float ("device.routes.default-source-volume")
                          or Settings.get_float ("device.routes.default-sink-volume") }

          -- prefix the props with correct IDs to create a Pod.Object
          table.insert (props, 1, "Spa:Pod:Object:Param:Props")
          table.insert (props, 2, "Route")

          -- convert arrays to Spa Pod
          if props.channelVolumes then
            table.insert (props.channelVolumes, 1, "Spa:Float")
            props.channelVolumes = Pod.Array (props.channelVolumes)
          end
          if props.channelMap then
            table.insert (props.channelMap, 1, "Spa:Enum:AudioChannel")
            props.channelMap = Pod.Array (props.channelMap)
          end
          if props.iec958Codecs then
            table.insert (props.iec958Codecs, 1, "Spa:Enum:AudioIEC958Codec")
            props.iec958Codecs = Pod.Array (props.iec958Codecs)
          end

          -- construct Route param
          local param = Pod.Object {
            "Spa:Pod:Object:Param:Route", "Route",
            index = route_info.index,
            device = device_id,
            props = Pod.Object (props),
            save = route_info.save,
          }

          log:debug (param,
            string.format ("setting route(%s) on for device(%s)(%s)",
              route_info.name, dev_info.name, tostring (device)))

          device:set_param ("Route", param)

          ::skip_route::
        end

        -- FIXME: add cancellability
        -- sync on the pipewire connection to ensure that the params
        -- have been configured on the remote device object
        Core.sync (function ()
          transition:advance ()
        end)
      end
    },
  }
}:register()
