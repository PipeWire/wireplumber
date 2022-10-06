-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

local cutils = require ("common-utils")

local defaults = {}
defaults.reserve_priority = -20
defaults.reserve_application_name = "WirePlumber"
defaults.jack_device = false
defaults.properties = Json.Object {}
defaults.vm_node_defaults = Json.Object {
  ["api.alsa.period-size"] = 256,
  ["api.alsa.headroom"] = 8192
}

local config = {}
config.reserve_priority = Settings.parse_int_safe (
    "monitor.alsa.reserve-priority", defaults.reserve_priority)
config.reserve_application_name = Settings.parse_string_safe (
    "monitor.alsa.reserve-application-name", defaults.reserve_application_name)
config.jack_device = Settings.parse_boolean_safe (
    "monitor.alsa.jack-device", defaults.jack_device)
config.properties = Settings.parse_object_safe (
    "monitor.alsa.properties", defaults.properties)
config.vm_node_defaults = Settings.parse_object_safe (
    "monitor.alsa.vm.node.defaults", defaults.vm_node_defaults)

-- unique device/node name tables
device_names_table = nil
node_names_table = nil

function nonempty(str)
  return str ~= "" and str or nil
end

function applyDefaultDeviceProperties (properties)
  properties["api.alsa.use-acp"] = true
  properties["api.acp.auto-port"] = false
end

function createNode(parent, id, obj_type, factory, properties)
  local dev_props = parent.properties

  -- set the device id and spa factory name; REQUIRED, do not change
  properties["device.id"] = parent["bound-id"]
  properties["factory.name"] = factory

  -- set the default pause-on-idle setting
  properties["node.pause-on-idle"] = false

  -- try to negotiate the max ammount of channels
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

    -- deduplicate nodes with the same name
    for counter = 2, 99, 1 do
      if node_names_table[properties["node.name"]] ~= true then
        node_names_table[properties["node.name"]] = true
        break
      end
      properties["node.name"] = name .. "." .. counter
    end
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

  -- apply VM overrides
  if nonempty(Core.get_vm_type()) and
      type(config.vm_node_defaults) == "table" then
    for k, v in pairs(config.vm_node_defaults) do
      properties[k] = v
    end
  end

  -- apply properties from rules defined in JSON .conf file
  cutils.evaluateRulesApplyProperties (properties, "monitor.alsa.rules")
  if properties["node.disabled"] then
    node_names_table [properties ["node.name"]] = nil
    return
  end

  -- create the node
  local node = Node("adapter", properties)
  node:activate(Feature.Proxy.BOUND)
  parent:store_managed_object(id, node)
end

function createDevice(parent, id, factory, properties)
  local device = SpaDevice(factory, properties)
  if device then
    device:connect("create-object", createNode)
    device:connect("object-removed", function (parent, id)
      local node = parent:get_managed_object(id)
      if not node then
        return
      end

      node_names_table[node.properties["node.name"]] = nil
    end)
    device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
    parent:store_managed_object(id, device)
  else
    Log.warning ("Failed to create '" .. factory .. "' device")
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
  local applied = cutils.evaluateRulesApplyProperties (properties, "monitor.alsa.rules")
  if not applied then
    applyDefaultDeviceProperties (properties)
  end
  if properties ["device.disabled"] then
    device_names_table [properties ["device.name"]] = nil
    return
  end

  -- override the device factory to use ACP
  if properties["api.alsa.use-acp"] then
    Log.info("Enabling the use of ACP on " .. properties["device.name"])
    factory = "api.alsa.acp.device"
  end

  -- use device reservation, if available
  if rd_plugin and properties["api.alsa.card"] then
    local rd_name = "Audio" .. properties["api.alsa.card"]
    local rd = rd_plugin:call("create-reservation",
        rd_name,
        config.reserve_application_name,
        properties["device.name"],
        config.reserve_priority);

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
        parent:store_managed_object(id, nil)
      end
    end)

    rd:connect("release-requested", function (rd)
        Log.info("release requested")
        parent:store_managed_object(id, nil)
        rd:call("release")
    end)

    if jack_device then
      rd:connect("notify::owner-name-changed", function (rd, pspec)
        if rd["state"] == "busy" and
           rd["owner-application-name"] == "Jack audio server" then
            -- TODO enable the jack device
        else
            -- TODO disable the jack device
        end
      end)
    end

    rd:call("acquire")
  else
    -- create the device
    createDevice(parent, id, factory, properties)
  end
end

function createMonitor ()
  local m = SpaDevice("api.alsa.enum.udev", config.properties)
  if m == nil then
    Log.message("PipeWire's SPA ALSA udev plugin(\"api.alsa.enum.udev\")"
      .. "missing or broken. Sound Cards cannot be enumerated")
    return nil
  end

  -- handle create-object to prepare device
  m:connect("create-object", prepareDevice)

  -- handle object-removed to destroy device reservations and recycle device name
  m:connect("object-removed", function (parent, id)
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
    for managed_node in device:iterate_managed_objects() do
      node_names_table[managed_node.properties["node.name"]] = nil
    end
  end)

  -- reset the name tables to make sure names are recycled
  device_names_table = {}
  node_names_table = {}

  -- activate monitor
  Log.info("Activating ALSA monitor")
  m:activate(Feature.SpaDevice.ENABLED)
  return m
end

-- create the JACK device (for PipeWire to act as client to a JACK server)
if config.jack_device then
  jack_device = Device("spa-device-factory", {
    ["factory.name"] = "api.jack.device",
    ["node.name"] = "JACK-Device",
  })
  jack_device:activate(Feature.Proxy.BOUND)
end

-- if the reserve-device plugin is enabled, at the point of script execution
-- it is expected to be connected. if it is not, assume the d-bus connection
-- has failed and continue without it
rd_plugin = Plugin.find("reserve-device")
if rd_plugin and rd_plugin:call("get-dbus")["state"] ~= "connected" then
  Log.message("reserve-device plugin is not connected to D-Bus, "
              .. "disabling device reservation")
  rd_plugin = nil
end

-- handle rd_plugin state changes to destroy and re-create the ALSA monitor in
-- case D-Bus service is restarted
if rd_plugin then
  local dbus = rd_plugin:call("get-dbus")
  dbus:connect("notify::state", function (b, pspec)
    local state = b["state"]
    Log.info ("rd-plugin state changed to " .. state)
    if state == "connected" then
      Log.info ("Creating ALSA monitor")
      monitor = createMonitor()
    elseif state == "closed" then
      Log.info ("Destroying ALSA monitor")
      monitor = nil
    end
  end)
end

-- create the monitor
monitor = createMonitor()
