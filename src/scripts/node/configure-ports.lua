-- WirePlumber
--
-- Copyright © 2026 The WirePlumber project contributors
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")

log = Log.open_topic ("s-node")


function evaluateNoDspOnAllStreams (no_dsp)
  local streams_om = cutils.get_object_manager ("stream")

  log:info (string.format ("updating no-dsp=%s on all streams",
      tostring (no_dsp)))

  for stream in streams_om:iterate {
      type = "AudioStream",
  } do
    local stream_id = stream.id
    local node_id = stream:get_node_bound_id ()
    local stream_props = stream:get_properties ()

    log:info (stream, string.format ("configuring stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    local conf_props = Properties ()
    conf_props ["stream.no-dsp"] = no_dsp
    stream:configure_ports_with_props (conf_props)
  end
end

Settings.subscribe ("node.features.audio.no-dsp", function ()
  local no_dsp = Settings.get_boolean ("node.features.audio.no-dsp")
  evaluateNoDspOnAllStreams (no_dsp)
end)


function evaluateMonitorPortsOnAllStreams (monitor_ports)
  local streams_om = cutils.get_object_manager ("stream")

  log:info (string.format ("updating monitor-ports=%s on all streams",
      tostring (monitor_ports)))

  for stream in streams_om:iterate {
      type = "AudioStream",
  } do
    local stream_id = stream.id
    local node_id = stream:get_node_bound_id ()
    local stream_props = stream:get_properties ()

    log:info (stream, string.format ("configuring stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    local conf_props = Properties ()
    conf_props ["stream.monitor-ports"] = monitor_ports
    stream:configure_ports_with_props (conf_props)
  end
end

Settings.subscribe ("node.features.audio.monitor-ports", function ()
  local monitor_ports = Settings.get_boolean ("node.features.audio.monitor-ports")
  evaluateMonitorPortsOnAllStreams (monitor_ports)
end)


function evaluateControlPortsOnAllStreams (control_ports)
  local streams_om = cutils.get_object_manager ("stream")

  log:info (string.format ("updating control-ports=%s on all streams",
      tostring (control_ports)))

  for stream in streams_om:iterate {
      type = "AudioStream",
  } do
    local stream_id = stream.id
    local node_id = stream:get_node_bound_id ()
    local stream_props = stream:get_properties ()

    log:info (stream, string.format ("configuring stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    local conf_props = Properties ()
    conf_props ["stream.control-ports"] = control_ports
    stream:configure_ports_with_props (conf_props)
  end
end

Settings.subscribe ("node.features.audio.control-port", function ()
  local control_ports = Settings.get_boolean ("node.features.audio.control-port")
  evaluateControlPortsOnAllStreams (control_ports)
end)


function evaluateMonoOnHardwareAudioStreams (mono_enabled)
  local streams_om = cutils.get_object_manager ("stream")

  log:info (string.format ("updating MONO=%s on hardware audio streams",
      tostring (mono_enabled)))

  for stream in streams_om:iterate {
      type = "AudioStream",
      Constraint { "factory.name", "c", "api.alsa.pcm.sink", "api.bluez5.a2dp.sink" },
  } do
    local stream_id = stream.id
    local node_id = stream:get_node_bound_id ()
    local stream_props = stream:get_properties ()

    log:info (stream, string.format ("configuring stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    local conf_props = Properties ()
    conf_props ["stream.mono-only"] = mono_enabled
    stream:configure_ports_with_props (conf_props)
  end
end

Settings.subscribe ("node.features.audio.mono", function ()
  local mono_enabled = Settings.get_boolean ("node.features.audio.mono")
  evaluateMonoOnHardwareAudioStreams (mono_enabled)
end)
