-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
log = Log.open_topic ("s-monitors")

defaults = {}
defaults.node_properties = {  -- Midi bridge node properties
  ["factory.name"] = "api.alsa.seq.bridge",

  -- Name set for the node with ALSA MIDI ports
  ["node.name"] = "Midi-Bridge",

  -- Set priorities so that it can be used as a fallback driver (see pipewire#3562)
  ["priority.session"] = "100",
  ["priority.driver"] = "1",
}

config = {}
config.monitoring = Core.test_feature ("monitor.alsa-midi.monitoring")
config.node_properties = Conf.get_section_as_properties (
    "monitor.alsa-midi.properties", defaults.node_properties)

SND_PATH = "/dev/snd"
SEQ_NAME = "seq"
SND_SEQ_PATH = SND_PATH .. "/" .. SEQ_NAME

midi_node = nil
fm_plugin = nil

function CreateMidiNode ()
  -- create the midi node
  local node = Node("spa-node-factory", config.node_properties)
  node:activate(Feature.Proxy.BOUND, function (n)
    log:info ("activated Midi bridge")
  end)

  return node;
end

if GLib.access (SND_SEQ_PATH, "rw") then
  midi_node = CreateMidiNode ()
elseif config.monitoring then
  fm_plugin = Plugin.find("file-monitor-api")
end

-- Only monitor the MIDI device if file does not exist and plugin API is loaded
if midi_node == nil and fm_plugin ~= nil then
  -- listen for changed events
  fm_plugin:connect ("changed", function (o, file, old, evtype)
    -- files attributes changed
    if evtype == "attribute-changed" then
      if file ~= SND_SEQ_PATH then
        return
      end
      if midi_node == nil and GLib.access (SND_SEQ_PATH, "rw") then
        midi_node = CreateMidiNode ()
        fm_plugin:call ("remove-watch", SND_PATH)
      end
    end

    -- directory is going to be unmounted
    if evtype == "pre-unmount" then
      fm_plugin:call ("remove-watch", SND_PATH)
    end
  end)

  -- add watch
  fm_plugin:call ("add-watch", SND_PATH, "m")
end
