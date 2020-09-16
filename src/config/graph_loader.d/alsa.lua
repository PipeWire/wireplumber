-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

--
-- ALSA monitor
--

static_object {
  type = "monitor",
  factory = "api.alsa.enum.udev",
  callbacks = {
    ["create-child"] = "alsaCreateDevice"
  }
}

local function alsaDeviceSetupProperties(properties)
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
end

function alsaCreateDevice(child_id, type, spa_factory, props, monitor_props)
  alsaDeviceSetupProperties(props)
  createChild (child_id, {
    type = "exported-device",
    factory = "api.alsa.acp.device",
    properties = props,
    callbacks = {
      ["create-child"] = "alsaCreateNode"
    }
  })
end

local function alsaNodeSetupProperties(properties, dev_props)
  local dev = properties["api.alsa.pcm.device"] or properties["alsa.device"] or "0"
  local subdev = properties["api.alsa.pcm.subdevice"] or properties["alsa.subdevice"] or "0"
  local stream = properties["api.alsa.pcm.stream"] or "unknown"
  local profile = properties["device.profile.name"] or "unknown"
  local profile_desc = properties["device.profile.description"]

  if not properties["media.class"] then
    if stream == "capture" then
      properties["media.class"] = "Audio/Source"
    else
      properties["media.class"] = "Audio/Sink"
    end
  end

  properties["node.nick"] = properties["node.nick"]
      or dev_props["device.nick"]
      or dev_props["api.alsa.card_name"]
      or dev_props["alsa.card_name"]

  properties["node.name"] = properties["node.name"]
      or (dev_props["device.name"] or "unknown") .. "." .. stream .. "." .. dev .. "." .. subdev

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
end

function alsaCreateNode(child_id, type, spa_factory, props, dev_props)
  alsaNodeSetupProperties(props, dev_props)
  props["factory.name"] = spa_factory
  createChild (child_id, {
    type = "node",
    factory = "adapter",
    properties = props,
  })
end
