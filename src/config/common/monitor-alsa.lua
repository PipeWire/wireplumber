-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

Config = {
  use_acp = true,
  --use_device_reservation = true,
  enable_midi = true,
  enable_jack_client = false,
}

if Config.enable_midi then
  midi_bridge = Node("spa-node-factory", {
    ["factory.name"] = "api.alsa.seq.bridge",
    ["node.name"] = "MIDI Bridge"
  })
end

if Config.enable_jack_client then
  jack_device = Device("spa-device-factory", {
    ["factory.name"] = "api.jack.device"
  })
end

function createNode(parent, id, type, factory, properties)
  local dev_props = parent.properties
  local dev = properties["api.alsa.pcm.device"] or properties["alsa.device"] or "0"
  local subdev = properties["api.alsa.pcm.subdevice"] or properties["alsa.subdevice"] or "0"
  local stream = properties["api.alsa.pcm.stream"] or "unknown"
  local profile = properties["device.profile.name"] or "unknown"
  local profile_desc = properties["device.profile.description"]

  -- ensure the node has a media class
  if not properties["media.class"] then
    if stream == "capture" then
      properties["media.class"] = "Audio/Source"
    else
      properties["media.class"] = "Audio/Sink"
    end
  end

  -- ensure the node has a name
  properties["node.nick"] = properties["node.nick"]
      or dev_props["device.nick"]
      or dev_props["api.alsa.card_name"]
      or dev_props["alsa.card_name"]

  properties["node.name"] = properties["node.name"]
      or (dev_props["device.name"] or "unknown") .. "." .. stream .. "." .. dev .. "." .. subdev

  -- ensure the node has a description
  if not properties["node.description"] then
    local desc = dev_props["device.description"] or "unknown"
    local name = properties["api.alsa.pcm.name"] or properties["api.alsa.pcm.id"] or dev

    if profile_desc then
      properties["node.description"] = desc .. " " .. profile_desc
    elseif subdev == "0" then
      properties["node.description"] = desc .. " (" .. name .. " " .. subdev .. ")"
    elseif dev == "0" then
      properties["node.description"] = desc .. " (" .. name .. ")"
    else
      properties["node.description"] = desc
    end
  end

  -- set the device id and spa factory name; REQUIRED, do not change
  properties["device.id"] = parent["bound-id"]
  properties["factory.name"] = factory

  -- create the node
  local node = Node("adapter", properties)
  node:activate(Feature.Proxy.BOUND)
  parent:store_managed_object(id, node)
end

function createDevice(parent, id, type, factory, properties)
  -- ensure the device has a name
  if not properties["device.name"] then
    local s = properties["device.bus-id"] or properties["device.bus-path"] or "unknown"
    properties["device.name"] = "alsa_card." .. s
  end

  -- ensure the device has a description
  if not properties["device.description"] then
    local d = nil
    local f = properties["device.form-factor"]
    local c = properties["device.class"]

    if f == "internal" then
      d = "Built-in Audio"
    elseif c == "modem" then
      d = "Modem"
    end

    d = d or properties["device.product.name"] or "Unknown device"
    properties["device.description"] = d
  end

  -- set the icon name
  if not properties["device.icon-name"] then
    local icon = nil
    local f = properties["device.form-factor"]
    local c = properties["device.class"]
    local b = properties["device.bus"]

    if f == "microphone" then
      icon = "audio-input-microphone"
    elseif f == "webcam" then
      icon = "camera-web"
    elseif f == "handset" then
      icon = "phone"
    elseif f == "portable" then
      icon = "multimedia-player"
    elseif f == "tv" then
      icon = "video-display"
    elseif f == "headset" then
      icon = "audio-headset"
    elseif f == "headphone" then
      icon = "audio-headphones"
    elseif f == "speaker" then
      icon = "audio-speakers"
    elseif f == "hands-free" then
      icon = "audio-handsfree"
    elseif c == "modem" then
      icon = "modem"
    end

    icon = icon or "audio-card"

    if b then b = ("-" .. b) else b = "" end
    properties["device.icon-name"] = icon .. "-analog" .. b
  end

  -- override the device factory to use ACP
  if Config.use_acp then
    factory = "api.alsa.acp.device"
  end

  -- create the device
  local device = SpaDevice(factory, properties)
  device:connect("create-object", createNode)
  device:activate(Feature.SpaDevice.ENABLED | Feature.Proxy.BOUND)
  parent:store_managed_object(id, device)
end

monitor = SpaDevice("api.alsa.enum.udev")
monitor:connect("create-object", createDevice)
monitor:activate(Feature.SpaDevice.ENABLED)
