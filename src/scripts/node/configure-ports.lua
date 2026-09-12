-- WirePlumber
--
-- Copyright © 2026 The WirePlumber project contributors
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")

log = Log.open_topic ("s-node")


function reconfigureAudioStreams ()
  local streams_om = cutils.get_object_manager ("stream")

  log:info ("reconfiguring the audio streams after a settings change")

  for stream in streams_om:iterate {
      type = "AudioStream",
  } do
    -- streams that we have not configured yet keep the port configuration
    -- the node came up with, until they are linked
    if stream:has_ports_configured () then
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
