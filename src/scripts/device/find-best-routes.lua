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
-- find the best route for a given device_id, based on availability and priority

cutils = require ("common-utils")
devinfo = require ("device-info-cache")
log = Log.open_topic ("s-device")

SimpleEventHook {
  name = "device/find-best-routes",
  after = "device/find-stored-routes",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-routes" },
      Constraint { "profile.active-device-ids", "is-present" },
    },
  },
  execute = function (event)
    local device = event:get_subject ()
    local event_properties = event:get_properties ()
    local active_ids = event_properties ["profile.active-device-ids"]
    local selected_routes = event:get_data ("selected-routes") or {}

    local dev_info = devinfo:get_device_info (device)
    assert (dev_info)

    -- active IDs are exchanged in JSON format
    active_ids = Json.Raw (active_ids):parse ()

    for _, device_id in ipairs (active_ids) do
      -- if a previous hook already selected a route for this device_id, skip it
      if selected_routes [tostring (device_id)] then
        goto next_device_id
      end

      local best_avail = nil
      local best_unk = nil
      for _, ri in pairs (dev_info.route_infos) do
        if cutils.arrayContains (ri.devices, device_id) and
              (ri.profiles == nil or cutils.arrayContains (ri.profiles, dev_info.active_profile)) then
          if ri.available == "yes" or ri.available == "unknown" then
            if ri.direction == "Output" and ri.available ~= ri.prev_available then
              best_avail = ri
              ri.save = true
              break
            elseif ri.available == "yes" then
              if (best_avail == nil or ri.priority > best_avail.priority) then
                best_avail = ri
              end
            elseif best_unk == nil or ri.priority > best_unk.priority then
              best_unk = ri
            end
          end
        end
      end

      local route = best_avail or best_unk
      if route then
        selected_routes [tostring (device_id)] =
            Json.Object { index = route.index }:to_string ()
      end

      ::next_device_id::
    end

    -- save the selected routes for the apply-routes hook
    event:set_data ("selected-routes", selected_routes)
  end
}:register ()
