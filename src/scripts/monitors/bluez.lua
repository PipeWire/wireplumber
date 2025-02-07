-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

COMBINE_OFFSET = 64
LOOPBACK_SOURCE_ID = 128
DEVICE_SOURCE_ID = 0

cutils = require ("common-utils")
log = Log.open_topic ("s-monitors")

config = {}
config.seat_monitoring = Core.test_feature ("monitor.bluez.seat-monitoring")
config.properties = Conf.get_section_as_properties ("monitor.bluez.properties")
config.rules = Conf.get_section_as_json ("monitor.bluez.rules", Json.Array {})

-- This is not a setting, it must always be enabled
config.properties["api.bluez5.connection-info"] = true

-- Properties used for previously creating a SCO source node. key: SPA device id
sco_source_node_properties = {}


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

function setOffloadActive(device, value)
  local pod = Pod.Object {
    "Spa:Pod:Object:Param:Props", "Props", bluetoothOffloadActive = value
  }
  device:set_params("Props", pod)
end

nodes_om:connect("object-added", function(_, node)
  node:connect("state-changed", function(node, old_state, cur_state)
    local interest = Interest {
      type = "device",
      Constraint { "object.id", "=", node.properties["device.id"]}
    }
    for d in devices_om:iterate (interest) do
      if cur_state == "running" then
        setOffloadActive(d, true)
      else
        setOffloadActive(d, false)
      end
    end
  end)
end)

function createOffloadScoNode(parent, id, type, factory, properties)
  local dev_props = parent.properties

  local args = {
    ["audio.channels"] = 1,
    ["audio.position"] = "[MONO]",
  }

  local desc =
      dev_props["device.description"]
      or dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "bluetooth-device"
  -- sanitize description, replace ':' with ' '
  args["node.description"] = desc:gsub("(:)", " ")

  if factory:find("sink") then
    local capture_args = {
      ["device.id"] = parent["bound-id"],
      ["media.class"] = "Audio/Sink",
      ["node.pause-on-idle"] = false,
    }
    for k, v in pairs(properties) do
      capture_args[k] = v
    end

    local name = "bluez_output" .. "." .. (properties["api.bluez5.address"] or dev_props["device.name"]) .. "." .. tostring(id)
    args["node.name"] = name:gsub("([^%w_%-%.])", "_")
    args["capture.props"] = Json.Object(capture_args)
    args["playback.props"] = Json.Object {
      ["node.passive"] = true,
      ["node.pause-on-idle"] = false,
      ["state.restore-props"] = false,
    }
  elseif factory:find("source") then
    local playback_args = {
      ["device.id"] = parent["bound-id"],
      ["media.class"] = "Audio/Source",
      ["node.pause-on-idle"] = false,
    }
    for k, v in pairs(properties) do
      playback_args[k] = v
    end

    local name = "bluez_input" .. "." .. (properties["api.bluez5.address"] or dev_props["device.name"]) .. "." .. tostring(id)
    args["node.name"] = name:gsub("([^%w_%-%.])", "_")
    args["capture.props"] = Json.Object {
      ["node.passive"] = true,
      ["node.pause-on-idle"] = false,
      ["state.restore-props"] = false,
    }
    args["playback.props"] = Json.Object(playback_args)
  else
    log:warning(parent, "Unsupported factory: " .. factory)
    return
  end

  -- Transform 'args' to a json object here
  local args_json = Json.Object(args)

  -- and get the final JSON as a string from the json object
  local args_string = args_json:get_data()

  local loopback_properties = {}

  local loopback = LocalModule("libpipewire-module-loopback", args_string, loopback_properties)
  parent:store_managed_object(id, loopback)
end

device_set_nodes_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "api.bluez5.set.leader", "+", type = "pw" },
  }
}

device_set_nodes_om:connect ("object-added", function(_, node)
    -- Connect ObjectConfig events to the right node
    if not monitor then
      return
    end

    local interest = Interest {
      type = "device",
      Constraint { "object.id", "=", node.properties["device.id"] }
    }
    log:info("Device set node found: " .. tostring (node["bound-id"]))
    for device in devices_om:iterate (interest) do
      local device_id = device.properties["spa.object.id"]
      if not device_id then
        goto next_device
      end

      local spa_device = monitor:get_managed_object (tonumber (device_id))
      if not spa_device then
        goto next_device
      end

      local id = node.properties["card.profile.device"]
      if id ~= nil then
        log:info(".. assign to device: " .. tostring (device["bound-id"]) .. " node " .. tostring (id))
        spa_device:store_managed_object (id, node)
      end

      ::next_device::
    end
end)

function createSetNode(parent, id, type, factory, properties)
  local args = {}
  local target_class
  local stream_class
  local rules = {}
  local members_json = Json.Raw (properties["api.bluez5.set.members"])
  local channels_json = Json.Raw (properties["api.bluez5.set.channels"])
  local members = members_json:parse ()
  local channels = channels_json:parse ()

  if properties["media.class"] == "Audio/Sink" then
    args["combine.mode"] = "sink"
    target_class = "Audio/Sink/Internal"
    stream_class = "Stream/Output/Audio/Internal"
  else
    args["combine.mode"] = "source"
    target_class = "Audio/Source/Internal"
    stream_class = "Stream/Input/Audio/Internal"
  end

  log:info("Device set: " .. properties["node.name"])

  for _, member in pairs(members) do
    log:info("Device set member:" .. member["object.path"])
    table.insert(rules,
      Json.Object {
        ["matches"] = Json.Array {
          Json.Object {
            ["object.path"] = member["object.path"],
            ["media.class"] = target_class,
          },
        },
        ["actions"] = Json.Object {
          ["create-stream"] = Json.Object {
            ["media.class"] = stream_class,
            ["audio.position"] = Json.Array (member["channels"]),
            ["state.restore-props"] = false,
          }
        },
      }
    )
  end

  properties["node.virtual"] = false
  properties["device.api"] = "bluez5"
  properties["api.bluez5.set.members"] = nil
  properties["api.bluez5.set.channels"] = nil
  properties["api.bluez5.set.leader"] = true
  properties["audio.position"] = Json.Array (channels)
  args["combine.props"] = Json.Object (properties)
  args["stream.props"] = Json.Object {}
  args["stream.rules"] = Json.Array (rules)

  local args_json = Json.Object(args)
  local args_string = args_json:get_data()
  local combine_properties = {}
  log:info("Device set node: " .. args_string)
  return LocalModule("libpipewire-module-combine-stream", args_string, combine_properties)
end

function createNode(parent, id, type, factory, properties)
  local dev_props = parent.properties
  local parent_id = parent["bound-id"]
  local parent_spa_id = tonumber(dev_props["spa.object.id"])

  if cutils.parseBool (config.properties ["bluez5.hw-offload-sco"]) and factory:find("sco") then
    createOffloadScoNode(parent, id, type, factory, properties)
    return
  end

  -- set the device id and spa factory name; REQUIRED, do not change
  properties["device.id"] = parent_id
  properties["factory.name"] = factory
  properties["spa.object.id"] = id

  -- set the default pause-on-idle setting
  properties["node.pause-on-idle"] = false

  -- set the node description
  local desc =
      dev_props["device.description"]
      or dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "bluetooth-device"
  -- sanitize description, replace ':' with ' '
  properties["node.description"] = desc:gsub("(:)", " ")

  local name_prefix = ((factory:find("sink") and "bluez_output") or
       (factory:find("source") and "bluez_input" or factory))

  -- hide the source node because we use the loopback source instead
  if parent:get_managed_object (LOOPBACK_SOURCE_ID) ~= nil and
       (factory == "api.bluez5.sco.source" or
         (factory == "api.bluez5.a2dp.source" and cutils.parseBool (properties["api.bluez5.a2dp-duplex"]))) then
    properties["bluez5.loopback-target"] = true
    properties["api.bluez5.internal"] = true
    -- add 'internal' to name prefix to not be confused with loopback node
    name_prefix = name_prefix .. "_internal"
  end

  -- set the node name
  local name = name_prefix .. "." ..
      (properties["api.bluez5.address"] or dev_props["device.name"]) .. "." ..
      tostring(id)
  -- sanitize name
  properties["node.name"] = name:gsub("([^%w_%-%.])", "_")

  -- set priority
  if not properties["priority.driver"] then
    local priority = factory:find("source") and 2010 or 1010
    properties["priority.driver"] = priority
    properties["priority.session"] = priority
  end

  -- autoconnect if it's a stream
  if properties["api.bluez5.profile"] == "headset-audio-gateway" or
     properties["api.bluez5.profile"] == "bap-sink" or
     factory:find("a2dp.source") or factory:find("media.source") then
    properties["node.autoconnect"] = true
  end

  -- apply properties from the rules in the configuration file
  properties = JsonUtils.match_rules_update_properties (config.rules, properties)

  -- create the node; bluez requires "local" nodes, i.e. ones that run in
  -- the same process as the spa device, for several reasons

  if properties["api.bluez5.set.leader"] then
    local combine = createSetNode(parent, id, type, factory, properties)
    parent:store_managed_object(id + COMBINE_OFFSET, combine)
    parent:set_managed_pending(id)
  else
    log:info("Create node: " .. properties["node.name"] .. ": " .. factory .. " " .. tostring (id))
    properties["bluez5.loopback"] = false
    if factory == "api.bluez5.sco.source" then
      sco_source_node_properties[parent_spa_id] = properties
    end
    local node = LocalNode("adapter", properties)
    node:activate(Feature.Proxy.BOUND)
    parent:store_managed_object(id, node)
  end
end

function removeNode(parent, id)
  local dev_props = parent.properties
  local parent_spa_id = tonumber(dev_props["spa.object.id"])
  local src_properties = sco_source_node_properties[parent_spa_id]

  log:debug("Remove node: " .. tostring (id))

  if src_properties ~= nil and id == tonumber(src_properties["spa.object.id"]) then
    log:debug("Clear old SCO properties")
    sco_source_node_properties[parent_spa_id] = nil
  end

  -- Clear also the device set module, if any
  parent:store_managed_object(id + COMBINE_OFFSET, nil)
end

function createDevice(parent, id, type, factory, properties)
  local device = parent:get_managed_object(id)
  if not device then
    -- ensure a proper device name
    local name =
        (properties["device.name"] or
         properties["api.bluez5.address"] or
         properties["device.description"] or
         tostring(id)):gsub("([^%w_%-%.])", "_")

    if not name:find("^bluez_card%.", 1) then
      name = "bluez_card." .. name
    end
    properties["device.name"] = name

    -- set the icon name
    if not properties["device.icon-name"] then
      local icon = nil
      local icon_map = {
        -- form factor -> icon
        ["microphone"] = "audio-input-microphone",
        ["webcam"] = "camera-web",
        ["handset"] = "phone",
        ["portable"] = "multimedia-player",
        ["tv"] = "video-display",
        ["headset"] = "audio-headset",
        ["headphone"] = "audio-headphones",
        ["speaker"] = "audio-speakers",
        ["hands-free"] = "audio-handsfree",
      }
      local f = properties["device.form-factor"]
      local b = properties["device.bus"]

      icon = icon_map[f] or "audio-card"
      properties["device.icon-name"] = icon .. (b and ("-" .. b) or "")
    end

    -- initial profile is to be set by policy-device-profile.lua, not spa-bluez5
    properties["bluez5.profile"] = "off"
    properties["spa.object.id"] = id

    -- apply properties from the rules in the configuration file
    properties = JsonUtils.match_rules_update_properties (config.rules, properties)

    -- create the device
    device = SpaDevice(factory, properties)
    if device then
      device:connect("create-object", createNode)
      device:connect("object-removed", removeNode)
      parent:store_managed_object(id, device)
    else
      log:warning ("Failed to create '" .. factory .. "' device")
      return
    end
  end

  log:info(parent, string.format("%d, %s (%s): %s",
        id, properties["device.description"],
        properties["api.bluez5.address"], properties["api.bluez5.connection"]))

  -- activate the device after the bluez profiles are connected
  if properties["api.bluez5.connection"] == "connected" then
    device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
  else
    device:deactivate(Features.ALL)
  end
end

function removeDevice(parent, id)
  sco_source_node_properties[id] = nil
end

function createMonitor()
  local monitor = SpaDevice("api.bluez5.enum.dbus", config.properties)
  if monitor then
    monitor:connect("create-object", createDevice)
    monitor:connect("object-removed", removeDevice)
  else
    log:notice("PipeWire's BlueZ SPA plugin is missing or broken. " ..
        "Bluetooth devices will not be supported.")
    return nil
  end
  monitor:activate(Feature.SpaDevice.ENABLED)

  return monitor
end

function CreateDeviceLoopbackSource (dev_name, dec_desc, dev_id)
  local args = Json.Object {
    ["capture.props"] = Json.Object {
      ["node.name"] = string.format ("bluez_capture_internal.%s", dev_name),
      ["media.class"] = "Stream/Input/Audio/Internal",
      ["node.description"] =
          string.format ("Bluetooth internal capture stream for %s", dec_desc),
      ["audio.channels"] = 1,
      ["audio.position"] = "[MONO]",
      ["bluez5.loopback"] = true,
      ["stream.dont-remix"] = true,
      ["node.passive"] = true,
      ["node.dont-fallback"] = true,
      ["node.linger"] = true,
      ["state.restore-props"] = false,
    },
    ["playback.props"] = Json.Object {
      ["node.name"] = string.format ("bluez_input.%s", dev_name),
      ["node.description"] = string.format ("%s", dec_desc),
      ["node.virtual"] = false,
      ["audio.position"] = "[MONO]",
      ["media.class"] = "Audio/Source",
      ["device.id"] = dev_id,
      ["card.profile.device"] = DEVICE_SOURCE_ID,
      ["device.routes"] = "1",
      ["priority.driver"] = 2010,
      ["priority.session"] = 2010,
      ["bluez5.loopback"] = true,
      ["filter.smart"] = true,
      ["filter.smart.target"] = Json.Object {
        ["bluez5.loopback-target"] = true,
        ["bluez5.loopback"] = false,
        ["device.id"] = dev_id
      }
    }
  }
  return LocalModule("libpipewire-module-loopback", args:get_data(), {})
end

function checkProfiles (dev)
  local device_id = dev["bound-id"]
  local props = dev.properties
  local device_spa_id = tonumber(props["spa.object.id"])

  -- Don't create loopback source device if autoswitch is disabled
  if not Settings.get_boolean ("bluetooth.autoswitch-to-headset-profile") then
    return
  end

  -- Get the associated BT SpaDevice
  local internal_id = tostring (props["spa.object.id"])
  local spa_device = monitor:get_managed_object (internal_id)
  if spa_device == nil then
    return
  end

  -- Ignore devices that don't support both A2DP sink and HSP/HFP profiles
  local has_a2dpsink_profile = false
  local has_headset_profile = false
  for p in dev:iterate_params("EnumProfile") do
    local profile = cutils.parseParam (p, "EnumProfile")
    if profile.name:find ("a2dp") and profile.name:find ("sink") then
      has_a2dpsink_profile = true
    elseif profile.name:find ("headset") then
      has_headset_profile = true
    end
  end
  if not has_a2dpsink_profile or not has_headset_profile then
    return
  end

  -- Create the loopback device if never created before
  local loopback = spa_device:get_managed_object (LOOPBACK_SOURCE_ID)
  if loopback == nil then
    local dev_name = props["api.bluez5.address"] or props["device.name"]
    local dec_desc = props["device.description"] or props["device.name"]
      or props["device.nick"] or props["device.alias"] or "bluetooth-device"

    log:info("create SCO loopback node: " .. dev_name)

    -- sanitize description, replace ':' with ' '
    dec_desc = dec_desc:gsub("(:)", " ")
    loopback = CreateDeviceLoopbackSource (dev_name, dec_desc, device_id)
    spa_device:store_managed_object(LOOPBACK_SOURCE_ID, loopback)

    -- recreate any sco source node
    local properties = sco_source_node_properties[device_spa_id]
    if properties ~= nil then
      local node_id = tonumber(properties["spa.object.id"])
      local node = spa_device:get_managed_object (node_id)
      if node ~= nil then
        log:info("Recreate node: " .. properties["node.name"] .. ": " ..
          properties["factory.name"] .. " " .. tostring (node_id))

        spa_device:store_managed_object(node_id, nil)

        properties["bluez5.loopback-target"] = true
        properties["api.bluez5.internal"] = true
        node = LocalNode("adapter", properties)
        node:activate(Feature.Proxy.BOUND)
        spa_device:store_managed_object(node_id, node)
      end
    end
  end
end

function onDeviceParamsChanged (dev, param_name)
  if param_name == "EnumProfile" then
    checkProfiles (dev)
  end
end

devices_om:connect("object-added", function(_, dev)
  -- Ignore all devices that are not BT devices
  if dev.properties["device.api"] ~= "bluez5" then
    return
  end

  -- check available profiles
  dev:connect ("params-changed", onDeviceParamsChanged)
  checkProfiles (dev)
end)

if config.seat_monitoring then
  logind_plugin = Plugin.find("logind")
end
if logind_plugin then
  -- if logind support is enabled, activate
  -- the monitor only when the seat is active
  function startStopMonitor(seat_state)
    log:info(logind_plugin, "Seat state changed: " .. seat_state)

    if seat_state == "active" then
      monitor = createMonitor()
    elseif monitor then
      monitor:deactivate(Feature.SpaDevice.ENABLED)
      monitor = nil
    end
  end

  logind_plugin:connect("state-changed", function(p, s) startStopMonitor(s) end)
  startStopMonitor(logind_plugin:call("get-state"))
else
  monitor = createMonitor()
end

nodes_om:activate()
devices_om:activate()
device_set_nodes_om:activate()
