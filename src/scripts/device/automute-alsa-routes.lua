-- WirePlumber
--
-- Copyright © 2025 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
log = Log.open_topic ("s-automute-alsa-routes")
hooks_registered = false

function setRoute (device, route, mute)
  local param = Pod.Object {
    "Spa:Pod:Object:Param:Route", "Route",
    index = route.index,
    device = route.device,
    props = Pod.Object {
      "Spa:Pod:Object:Param:Props", "Route",
      mute = mute
    },
    save = false,
  }

  log:info (device, "Setting mute to " .. tostring(mute) ..
      " on route " .. route.name)
  device:set_param("Route", param)
end

function findLowestPriorityAvailableOutputRoute (device)
  local lowest_prio_r = nil
  for p in device:iterate_params("Route") do
    local route = cutils.parseParam (p, "Route")
    if route and route.direction == "Output" and route.available ~= "no" then
      if lowest_prio_r == nil or lowest_prio_r.priority > route.priority then
        lowest_prio_r = route
      end
    end
  end
  return lowest_prio_r
end

function evaluateNode (node, source)
  if nodes_info [node.id] == nil then
    return
  end

  -- Get node info
  local node_state = nodes_info [node.id].state
  local node_api = nodes_info [node.id].api
  local node_dev_id = nodes_info [node.id].dev_id
  local node_cpd = nodes_info [node.id].cpd

  -- Don't do anything if node was not running
  if node_state ~= "running" then
    return
  end

  -- Emite event if setting is enabled for this API
  local mute_alsa = Settings.get_boolean ("device.routes.mute-on-alsa-playback-removed")
  local mute_bluez = Settings.get_boolean ("device.routes.mute-on-bluetooth-playback-removed")
  if (mute_alsa and node_api == "alsa") or
      (mute_bluez and node_api == "bluez5") then
    local e = source:call ("create-event", "mute-alsa-devices", nil, nil)
    e:set_data ("device-id", node_dev_id)
    e:set_data ("card-profile-device", node_cpd)
    EventDispatcher.push_event (e)
  end
end

mute_alsa_devices_hook = SimpleEventHook {
  name = "device/mute-alsa-devices",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "mute-alsa-devices" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local dev_id = tonumber (event:get_data ("device-id"))
    local cpd = tonumber (event:get_data ("card-profile-device"))
    local device_om = source:call ("get-object-manager", "device")
    local send_notification = false

    -- We mute all available output ALSA routes but the one associated with
    -- the running node.
    --
    -- We also don't mute any routes if the running node is associated with
    -- the lowest priority route as this is most likely to be the Speakers.
    --
    -- For instance, we want to mute all routes except the Headphones one
    -- when unplugging a headset while playing audio, but we don't
    -- want to mute the Headphones route when a headset is plugged in while
    -- playing audio on the Speakers.
    for device in device_om:iterate() do
      local dev_bound_id = device["bound-id"]
      if device.properties["device.api"] == "alsa" then
        local lpr = findLowestPriorityAvailableOutputRoute (device)
        if lpr == nil or lpr.device ~= cpd or dev_bound_id ~= dev_id then
          for p in device:iterate_params("Route") do
            local route = cutils.parseParam (p, "Route")
            if route and
                route.direction == "Output" and
                route.available ~= "no" and
                (route.device ~= cpd or dev_bound_id ~= dev_id) then
              setRoute (device, route, true)
              send_notification = true
            end
          end
        end
      end
    end

    -- Send notification if devices were muted
    notifications = notifications or Plugin.find("notifications-api")
    if notifications ~= nil and send_notification then
      notifications:call ("send", I18n.gettext("Audio was auto-muted"),
          I18n.gettext("Active playback device was disconnected"))
    end
  end
}

update_nodes_info_hook = SimpleEventHook {
  name = "device/update-nodes-info",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-state-changed" },
      Constraint { "media.class", "matches", "Audio/Sink" },
      Constraint { "device.api", "+" },
      Constraint { "device.id", "+" },
      Constraint { "card.profile.device", "+", type = "pw" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local node_id = node["bound-id"]
    local device_api = node.properties ["device.api"]
    local device_id = node.properties ["device.id"]
    local cpd = node.properties ["card.profile.device"]
    local new_state = event:get_properties ()["event.subject.new-state"]

    -- Update node info
    if nodes_info [node.id] == nil then
      nodes_info [node.id] = {}
    end
    nodes_info [node.id].api = device_api
    nodes_info [node.id].state = new_state
    nodes_info [node.id].dev_id = device_id
    nodes_info [node.id].cpd = cpd
  end
}

evaluate_mute_on_device_route_changed_hook = SimpleEventHook {
  name = "device/evaluate-mute-on-device-route-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "EnumRoute" }
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local device = event:get_subject ()
    local node_om = source:call ("get-object-manager", "node")

    -- Evaluate all nodes for this device when the EnumRoute param changed
    for node in node_om:iterate {
        Constraint { "media.class", "matches", "Audio/Sink", type = "pw-global" },
        Constraint { "device.id", "=", device["bound-id"], type = "pw-global" },
      } do
      evaluateNode (node, source)
    end
  end
}

evaluate_mute_on_node_removed_hook = SimpleEventHook {
  name = "device/evaluate-mute-on-node-removed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-removed" },
      Constraint { "media.class", "matches", "Audio/Sink" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local node = event:get_subject ()

    -- Evaluate removed node
    evaluateNode (node, source)

    -- Clear removed node info
    nodes_info [node.id] = nil
  end
}

function toggleState ()
  local mute_alsa = Settings.get_boolean ("device.routes.mute-on-alsa-playback-removed")
  local mute_bluez = Settings.get_boolean ("device.routes.mute-on-bluetooth-playback-removed")
  if (mute_alsa or mute_bluez) and not hooks_registered then
    nodes_info = {}
    mute_alsa_devices_hook:register ()
    update_nodes_info_hook:register ()
    evaluate_mute_on_device_route_changed_hook:register ()
    evaluate_mute_on_node_removed_hook:register ()
    hooks_registered = true
  elseif not mute_alsa and not mute_bluez and hooks_registered then
    mute_alsa_devices_hook:remove ()
    update_nodes_info_hook:remove ()
    evaluate_mute_on_device_route_changed_hook:remove ()
    evaluate_mute_on_node_removed_hook:remove ()
    hooks_registered = false
  end
end

Settings.subscribe ("device.routes.mute-on-alsa-playback-removed", function ()
  toggleState ()
end)
Settings.subscribe ("device.routes.mute-on-bluetooth-playback-removed", function ()
  toggleState ()
end)
toggleState ()
