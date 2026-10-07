-- WirePlumber
--
-- Copyright © 2026 The WirePlumber project contributors
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")

log = Log.open_topic ("s-node")


function reconfigureAudioStreams ()
  local stream_links_om = cutils.get_object_manager ("stream-link")
  local streams_om = cutils.get_object_manager ("stream")
  local linked = {}

  log:info ("reconfiguring the audio streams after a settings change")

  -- a linked stream is reconfigured through its stream link: both ends of a
  -- link have to agree on one port configuration, and the link is what decides
  -- which one that is
  for stream_link in stream_links_om:iterate {
      type = "AudioStreamLink",
  } do
    if stream_link:reconfigure_ports () then
      local output = stream_link:get_output_stream ()
      local input = stream_link:get_input_stream ()

      log:info (stream_link, string.format (
          "configuring the link between stream:%d and stream:%d",
          output.id, input.id))

      linked [output.id] = true
      linked [input.id] = true
    end
  end

  for stream in streams_om:iterate {
      type = "AudioStream",
  } do
    -- streams that we have not configured yet keep the port configuration
    -- the node came up with, until they are linked
    if stream:has_ports_configured () and not linked [stream.id] then
      local stream_id = stream.id
      local node_id = stream:get_node_bound_id ()
      local stream_props = stream:get_properties ()

      log:info (stream, string.format ("configuring stream:%d (node:%d): %s",
          stream_id, node_id, tostring (stream_props["node.name"])))

      stream:configure_ports_with_props ()
    end
  end
end

Settings.subscribe ("node.features.audio.*", reconfigureAudioStreams)

-- Configures the ports of new audio streams that are not linked right away
-- (targets, non-autoconnect and dont-remix streams), since a node with no
-- port configuration can't run. Already configured streams are left alone.
SimpleEventHook {
  name = "node/configure-ports-on-stream-added",
  before = "linking/stream-added-immediate",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()

    if not Interest { type = "AudioStream" }:matches (stream) or
        stream:get_ports_mode () ~= "none" then
      return
    end

    local stream_props = stream:get_properties ()
    if stream:get_kind () ~= StreamKind.TARGET and
        stream_props:get_boolean ("node.autoconnect") and
        not stream_props:get_boolean ("stream.dont-remix") then
      return
    end

    log:info (stream, string.format (
        "configuring the ports of new stream:%d (node:%d): %s", stream.id,
        stream:get_node_bound_id (), tostring (stream_props ["node.name"])))

    stream:configure_ports_with_props ()
  end
}:register ()
