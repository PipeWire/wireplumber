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

function evaluateStream (stream, source)
  local info = streams_info [stream.id]
  if info == nil then
    return
  end

  -- Don't do anything if the stream node was not running
  if info.state ~= "running" then
    return
  end

  -- Emite event if setting is enabled for this API
  local mute_alsa = Settings.get_boolean ("device.routes.mute-on-alsa-playback-removed")
  local mute_bluez = Settings.get_boolean ("device.routes.mute-on-bluetooth-playback-removed")
  if (mute_alsa and info.api == "alsa") or
      (mute_bluez and info.api == "bluez5") then
    local e = source:call ("create-event", "mute-alsa-devices", nil, nil)
    e:set_data ("device-id", info.dev_id)
    e:set_data ("card-profile-device", info.cpd)
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
    -- the running stream.
    --
    -- We also don't mute any routes if the running stream is associated with
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

-- The stream info is kept while the stream node exists, as the node is no
-- longer available when the stream is removed
add_streams_info_hook = SimpleEventHook {
  name = "device/add-streams-info",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "shape", "=", StreamShape.SINK, type = "gobject" },
      Constraint { "device.api", "+" },
      Constraint { "device.id", "+" },
      Constraint { "card.profile.device", "+", type = "pw" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local stream_props = stream:get_properties ()
    local node = stream:get_node ()

    streams_info [stream.id] = {
      node_id = stream:get_node_bound_id (),
      api = stream_props ["device.api"],
      dev_id = stream_props ["device.id"],
      cpd = stream_props ["card.profile.device"],
      state = node and node.state or nil,
    }
  end
}

-- Streams do not have a state, so the state of their node is used
update_streams_state_hook = SimpleEventHook {
  name = "device/update-streams-state",
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
    local new_state = event:get_properties ()["event.subject.new-state"]

    for _, info in pairs (streams_info) do
      if info.node_id == node_id then
        info.state = new_state
      end
    end
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
    local stream_om = source:call ("get-object-manager", "stream")

    -- Evaluate all streams for this device when the EnumRoute param changed
    for stream in stream_om:iterate {
        type = "AudioStream",
        Constraint { "shape", "=", StreamShape.SINK, type = "gobject" },
        Constraint { "device.id", "=", device["bound-id"], type = "pw" },
      } do
      evaluateStream (stream, source)
    end
  end
}

evaluate_mute_on_stream_removed_hook = SimpleEventHook {
  name = "device/evaluate-mute-on-stream-removed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-removed" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "shape", "=", StreamShape.SINK, type = "gobject" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream = event:get_subject ()

    -- Evaluate removed stream
    evaluateStream (stream, source)

    -- Clear removed stream info
    streams_info [stream.id] = nil
  end
}

function toggleState ()
  local mute_alsa = Settings.get_boolean ("device.routes.mute-on-alsa-playback-removed")
  local mute_bluez = Settings.get_boolean ("device.routes.mute-on-bluetooth-playback-removed")
  if (mute_alsa or mute_bluez) and not hooks_registered then
    streams_info = {}
    mute_alsa_devices_hook:register ()
    add_streams_info_hook:register ()
    update_streams_state_hook:register ()
    evaluate_mute_on_device_route_changed_hook:register ()
    evaluate_mute_on_stream_removed_hook:register ()
    hooks_registered = true
  elseif not mute_alsa and not mute_bluez and hooks_registered then
    mute_alsa_devices_hook:remove ()
    add_streams_info_hook:remove ()
    update_streams_state_hook:remove ()
    evaluate_mute_on_device_route_changed_hook:remove ()
    evaluate_mute_on_stream_removed_hook:remove ()
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
