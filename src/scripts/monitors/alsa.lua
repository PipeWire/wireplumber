-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

SPLIT_PCM_PARENT_OFFSET = 256
SPLIT_PCM_OFFSET = 512

cutils = require ("common-utils")
log = Log.open_topic ("s-monitors")

config = {}
config.reserve_device = Core.test_feature ("monitor.alsa.reserve-device")
config.properties = Conf.get_section_as_properties ("monitor.alsa.properties")
config.rules = Conf.get_section_as_json ("monitor.alsa.rules", Json.Array {})

-- unique device/node name tables
device_names_table = nil
node_names_table = nil

-- SPA ids to node names: name = id_name_table[device_id][node_id]
id_name_table = nil


function nonempty(str)
  return str ~= "" and str or nil
end

function applyDefaultDeviceProperties (properties)
  properties["api.alsa.use-acp"] = true
  properties["api.acp.auto-profile"] = false
  properties["api.acp.auto-port"] = false
  properties["api.dbus.ReserveDevice1.Priority"] = -20
  properties["api.alsa.split-enable"] = true
end

function createSplitPCMHWNode(dev_props, properties)
  local skip_keys = {
    "api.alsa.split.position", "card.profile.device", "device.profile.description",
    "device.profile.name"
  }
  local props = {}

  for k, v in pairs(properties) do
    props[k] = v
  end
  for _, k in pairs(skip_keys) do
    props[k] = nil
  end

  -- create the underlying hidden ALSA node
  props["node.name"] = props["api.alsa.split.name"]
  props["node.description"] = string.format("%s %s", dev_props["device.description"],
        props["api.alsa.path"]:gsub("^[^,]*[,:]", ""))
  if props["api.alsa.pcm.stream"] == "capture" then
    props["media.class"] = "Audio/Source/Internal"
  else
    props["media.class"] = "Audio/Sink/Internal"
  end
  props["api.alsa.use-chmap"] = false
  props["api.alsa.split.parent"] = true
  props["audio.position"] = props["api.alsa.split.hw-position"]
  local channels = Json.Raw (props["api.alsa.split.hw-position"]):parse ()
  props["audio.channels"] = tostring(#channels)

  props = JsonUtils.match_rules_update_properties (config.rules, props)

  if cutils.parseBool (props ["node.disabled"]) then
    log:notice ("ALSA node " .. props ["node.name"] .. " disabled")
    return nil
  end

  return Node("adapter", props)
end

function createSplitPCMLoopback(parent, id, obj_type, factory, properties)
  local skip_keys = {
    -- not suitable for loopback
    "audio.rate",
    "clock.quantum-limit",
    "factory.name",
    "node.driver",
    "node.pause-on-idle",
    "node.want-driver",
    "port.group",
    "priority.driver",
    "resample.disable",
    "resample.prefill",
  }
  local args
  local props = {}

  props["node.virtual"] = false

  for k, v in pairs(properties) do
    props[k] = v
  end
  for _, k in pairs(skip_keys) do
    props[k] = nil
  end

  local split_props = {
    ["node.name"] = properties["node.name"] .. ".split",
    ["node.description"] = string.format(I18n.gettext("Split %s"), properties["node.description"]),
    ["audio.position"] = properties["api.alsa.split.position"],
    ["stream.dont-remix"] = true,
    ["node.passive"] = true,
    ["node.dont-fallback"] = true,
    ["node.linger"] = true,
    ["state.restore-props"] = false,
    ["target.object"] = properties["api.alsa.split.name"],
  }

  if properties["api.alsa.pcm.stream"] == "playback" then
    props["media.class"] = "Audio/Sink"
    split_props["media.class"] = "Stream/Output/Audio/Internal"
    args = Json.Object {
      ["capture.props"] = Json.Object (props),
      ["playback.props"] = Json.Object (split_props),
    }
  else
    props["media.class"] = "Audio/Source"
    split_props["media.class"] = "Stream/Input/Audio/Internal"
    args = Json.Object {
      ["playback.props"] = Json.Object (props),
      ["capture.props"] = Json.Object (split_props),
    }
  end

  return LocalModule("libpipewire-module-loopback", args:get_data(), {})
end

devices_om = ObjectManager {
  Interest {
    type = "device",
  }
}

split_nodes_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "api.alsa.split.position", "+", type = "pw" },
  }
}

split_nodes_om:connect ("object-added", function(_, node)
    -- Connect ObjectConfig events to the right node
    if not monitor then
      return
    end

    local interest = Interest {
      type = "device",
      Constraint { "object.id", "=", node.properties["device.id"] }
    }
    log:info("Split PCM node found: " .. tostring (node["bound-id"]))

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

function createNode(parent, id, obj_type, factory, properties)
  local dev_props = parent.properties
  local parent_id = tonumber(dev_props["spa.object.id"])

  -- set the device id and spa factory name; REQUIRED, do not change
  properties["device.id"] = parent["bound-id"]
  properties["factory.name"] = factory

  -- set the default pause-on-idle setting
  properties["node.pause-on-idle"] = false

  -- try to negotiate the max amount of channels
  if dev_props["api.alsa.use-acp"] ~= "true" then
    properties["audio.channels"] = properties["audio.channels"] or "64"
  end

  local dev = properties["api.alsa.pcm.device"]
              or properties["alsa.device"] or "0"
  local subdev = properties["api.alsa.pcm.subdevice"]
                 or properties["alsa.subdevice"] or "0"
  local stream = properties["api.alsa.pcm.stream"] or "unknown"
  local profile = properties["device.profile.name"]
                  or (stream .. "." .. dev .. "." .. subdev)
  local profile_desc = properties["device.profile.description"]

  -- set priority
  if not properties["priority.driver"] then
    local priority = (dev == "0") and 1000 or 744
    if stream == "capture" then
      priority = priority + 1000
    end

    priority = priority - (tonumber(dev) * 16) - tonumber(subdev)

    if profile:find("^pro%-") then
      priority = priority + 500
    elseif profile:find("^analog%-") then
      priority = priority + 9
    elseif profile:find("^iec958%-") then
      priority = priority + 8
    end

    if dev_props["device.bus"] == "usb" then
      priority = priority + 100
    end

    properties["priority.driver"] = priority
    properties["priority.session"] = priority
  end

  -- ensure the node has a media class
  if not properties["media.class"] then
    if stream == "capture" then
      properties["media.class"] = "Audio/Source"
    else
      properties["media.class"] = "Audio/Sink"
    end
  end

  -- ensure the node has a name
  if not properties["node.name"] then
    local name =
        (stream == "capture" and "alsa_input" or "alsa_output")
        .. "." ..
        (dev_props["device.name"]:gsub("^alsa_card%.(.+)", "%1") or
         dev_props["device.name"] or
         "unnamed-device")
         .. "." ..
         profile

    -- sanitize name
    name = name:gsub("([^%w_%-%.])", "_")

    properties["node.name"] = name

    log:info ("Creating node " .. name)

    -- deduplicate nodes with the same name
    for counter = 2, 99, 1 do
      if node_names_table[properties["node.name"]] ~= true then
        break
      end
      properties["node.name"] = name .. "." .. counter
      log:info ("deduplicating node name -> " .. properties["node.name"])
    end
  else
    log:info ("Creating node " .. properties["node.name"])
  end

  -- and a nick
  local nick = nonempty(properties["node.nick"])
      or nonempty(properties["api.alsa.pcm.name"])
      or nonempty(properties["alsa.name"])
      or nonempty(profile_desc)
      or dev_props["device.nick"]
  if nick == "USB Audio" then
    nick = dev_props["device.nick"]
  end
  -- also sanitize nick, replace ':' with ' '
  properties["node.nick"] = nick:gsub("(:)", " ")

  -- ensure the node has a description
  if not properties["node.description"] then
    local desc = nonempty(dev_props["device.description"]) or "unknown"
    local name = nonempty(properties["api.alsa.pcm.name"]) or
                 nonempty(properties["api.alsa.pcm.id"]) or dev

    if profile_desc then
      desc = desc .. " " .. profile_desc
    elseif subdev ~= "0" then
      desc = desc .. " (" .. name .. " " .. subdev .. ")"
    elseif dev ~= "0" then
      desc = desc .. " (" .. name .. ")"
    end

    -- also sanitize description, replace ':' with ' '
    properties["node.description"] = desc:gsub("(:)", " ")
  end

  -- add api.alsa.card.* properties for rule matching purposes
  for k, v in pairs(dev_props) do
    if k:find("^api%.alsa%.card%..*") then
      properties[k] = v
    end
  end

  -- add cpu.vm.name for rule matching purposes
  local vm_type = Core.get_vm_type()
  if nonempty(vm_type) then
    properties["cpu.vm.name"] = vm_type
  end

  -- apply properties from rules defined in JSON .conf file
  local orig_properties = {}
  for k, v in pairs(properties) do
    orig_properties[k] = v
  end
  properties = JsonUtils.match_rules_update_properties (config.rules, properties)

  if cutils.parseBool (properties ["node.disabled"]) then
    log:notice ("ALSA node " .. properties["node.name"] .. " disabled")
    return
  end

  node_names_table[properties["node.name"]] = true
  id_name_table[parent_id][id] = properties["node.name"]

  -- handle split HW node
  if properties["api.alsa.split.position"] ~= nil then
    local split_hw_node_name = string.format("%s.%s",
      (stream == "capture" and "alsa_input" or "alsa_output"),
      properties["api.alsa.path"]:gsub("([:,])", "_"))
    properties["api.alsa.split.name"] = split_hw_node_name
    orig_properties["api.alsa.split.name"] = split_hw_node_name

    if not node_names_table [split_hw_node_name] then
      log:info ("Create ALSA SplitPCM HW node " .. split_hw_node_name)

      local node = createSplitPCMHWNode(dev_props, orig_properties)
      if node ~= nil then
        node:activate(Feature.Proxy.BOUND)
        parent:store_managed_object(SPLIT_PCM_PARENT_OFFSET + id, node)

        node_names_table[split_hw_node_name] = true
        id_name_table[parent_id][SPLIT_PCM_PARENT_OFFSET + id] = split_hw_node_name
      end
    end

    -- create split PCM node
    log:info ("Create ALSA SplitPCM split node " .. properties["node.name"])

    local loopback = createSplitPCMLoopback (parent, id, obj_type, factory, properties)
    parent:store_managed_object(SPLIT_PCM_OFFSET + id, loopback)
    parent:set_managed_pending(id)
    return
  end

  -- create the node
  local node = Node("adapter", properties)
  parent:set_managed_pending(id)
  node:activate(Feature.Proxy.BOUND, function (_, err)
      if err then
        log:warning ("Failed to create " .. properties ["node.name"]
          .. ": " .. tostring(err))
      end
      parent:store_managed_object(id, node)
  end)
end

function removeNode(parent, id)
  local parent_id = tonumber(parent.properties["spa.object.id"])
  local ids = {id, SPLIT_PCM_PARENT_OFFSET + id, SPLIT_PCM_OFFSET + id}

  for _, j in pairs(ids) do
    local node_name = id_name_table[parent_id][j]

    parent:store_managed_object(j, nil)

    if node_name ~= nil then
      log:info ("Removing node " .. node_name)
      node_names_table[node_name] = nil
      id_name_table[parent_id][j] = nil
    end
  end
end

function createDevice(parent, id, factory, properties)
  id_name_table[id] = {}
  properties["spa.object.id"] = id
  local device = SpaDevice(factory, properties)
  if device then
    device:connect("create-object", createNode)
    device:connect("object-removed", removeNode)
    device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
    parent:store_managed_object(id, device)
  else
    log:warning ("Failed to create '" .. factory .. "' device")
  end
end

function removeDevice(parent, id)
  if id_name_table[id] ~= nil then
    for _, node_name in pairs(id_name_table[id]) do
      log:info ("Release " .. node_name)
      node_names_table[node_name] = nil
    end
    id_name_table[id] = nil
  end
end

function prepareDevice(parent, id, obj_type, factory, properties)
  -- ensure the device has an appropriate name
  local name = "alsa_card." ..
    (properties["device.name"] or
     properties["device.bus-id"] or
     properties["device.bus-path"] or
     tostring(id)):gsub("([^%w_%-%.])", "_")

  properties["device.name"] = name

  -- deduplicate devices with the same name
  for counter = 2, 99, 1 do
    if device_names_table[properties["device.name"]] ~= true then
      device_names_table[properties["device.name"]] = true
      break
    end
    properties["device.name"] = name .. "." .. counter
  end

  -- ensure the device has a description
  if not properties["device.description"] then
    local d = nil
    local f = properties["device.form-factor"]
    local c = properties["device.class"]
    local n = properties["api.alsa.card.name"]

    if n == "Loopback" then
      d = I18n.gettext("Loopback")
    elseif f == "internal" then
      d = I18n.gettext("Built-in Audio")
    elseif c == "modem" then
      d = I18n.gettext("Modem")
    end

    d = d or properties["device.product.name"]
          or properties["api.alsa.card.name"]
          or properties["alsa.card_name"]
          or "Unknown device"
    properties["device.description"] = d
  end

  -- ensure the device has a nick
  properties["device.nick"] =
      properties["device.nick"] or
      properties["api.alsa.card.name"] or
      properties["alsa.card_name"]

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
    local c = properties["device.class"]
    local b = properties["device.bus"]

    icon = icon_map[f] or ((c == "modem") and "modem") or "audio-card"
    properties["device.icon-name"] = icon .. "-analog" .. (b and ("-" .. b) or "")
  end

  -- apply properties from rules defined in JSON .conf file
  applyDefaultDeviceProperties (properties)
  properties = JsonUtils.match_rules_update_properties (config.rules, properties)

  if cutils.parseBool (properties ["device.disabled"]) then
    log:notice ("ALSA card/device " .. properties ["device.name"] .. " disabled")
    device_names_table [properties ["device.name"]] = nil
    return
  end

  -- override the device factory to use ACP
  if cutils.parseBool (properties ["api.alsa.use-acp"]) then
    log:info("Enabling the use of ACP on " .. properties["device.name"])
    factory = "api.alsa.acp.device"
  end

  -- use device reservation, if available
  if rd_plugin and properties["api.alsa.card"] then
    local rd_name = "Audio" .. properties["api.alsa.card"]
    local rd = rd_plugin:call("create-reservation",
        rd_name,
        cutils.get_application_name (),
        properties["device.name"],
        properties["api.dbus.ReserveDevice1.Priority"]);

    properties["api.dbus.ReserveDevice1"] = rd_name

    -- unlike pipewire-media-session, this logic here keeps the device
    -- acquired at all times and destroys it if someone else acquires
    rd:connect("notify::state", function (rd, pspec)
      local state = rd["state"]

      if state == "acquired" then
        -- create the device
        createDevice(parent, id, factory, properties)

      elseif state == "available" then
        -- attempt to acquire again
        rd:call("acquire")

      elseif state == "busy" then
        -- destroy the device
        removeDevice(parent, id)
        parent:store_managed_object(id, nil)
      end
    end)

    rd:connect("release-requested", function (rd)
        log:info("release requested")
        parent:store_managed_object(id, nil)
        rd:call("release")
    end)

    rd:call("acquire")
  else
    -- create the device
    createDevice(parent, id, factory, properties)
  end
end

function createMonitor ()
  local m = SpaDevice("api.alsa.enum.udev", config.properties)
  if m == nil then
    log:notice("PipeWire's ALSA SPA plugin is missing or broken. " ..
        "Sound cards will not be supported")
    return nil
  end

  -- handle create-object to prepare device
  m:connect("create-object", prepareDevice)

  -- handle object-removed to destroy device reservations and recycle device name
  m:connect("object-removed", function (parent, id)
    removeDevice(parent, id)

    local device = parent:get_managed_object(id)
    if not device then
      return
    end

    if rd_plugin then
      local rd_name = device.properties["api.dbus.ReserveDevice1"]
      if rd_name then
        rd_plugin:call("destroy-reservation", rd_name)
      end
    end
    device_names_table[device.properties["device.name"]] = nil
  end)

  -- reset the name tables to make sure names are recycled
  device_names_table = {}
  node_names_table = {}
  id_name_table = {}

  -- activate monitor
  log:info("Activating ALSA monitor")
  m:activate(Feature.SpaDevice.ENABLED)
  return m
end

-- if the reserve-device plugin is enabled, at the point of script execution
-- it is expected to be connected. if it is not, assume the d-bus connection
-- has failed and continue without it
if config.reserve_device then
  rd_plugin = Plugin.find("reserve-device")
end
if rd_plugin and rd_plugin:call("get-dbus")["state"] ~= "connected" then
  log:notice("reserve-device plugin is not connected to D-Bus, "
              .. "disabling device reservation")
  rd_plugin = nil
end

-- handle rd_plugin state changes to destroy and re-create the ALSA monitor in
-- case D-Bus service is restarted
if rd_plugin then
  local dbus = rd_plugin:call("get-dbus")
  dbus:connect("notify::state", function (b, pspec)
    local state = b["state"]
    log:info ("rd-plugin state changed to " .. state)
    if state == "connected" then
      log:info ("Creating ALSA monitor")
      monitor = createMonitor()
    elseif state == "closed" then
      log:info ("Destroying ALSA monitor")
      monitor = nil
    end
  end)
end

-- create the monitor
monitor = createMonitor()

devices_om:activate()
split_nodes_om:activate()
