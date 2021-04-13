#!/usr/bin/wpexec
--
-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT
--

-- Load the necessary wireplumber api modules
Core.require_api("default-nodes", "mixer", function(...)
  local default_nodes, mixer = ...

  -- configure volumes to be printed in the cubic scale
  -- this is also what the pulseaudio API shows
  mixer.scale = "cubic"

  local id = default_nodes:call("get-default-node", "Audio/Sink")
  local volume = mixer:call("get-volume", id)

  -- dump everything
  Debug.dump_table(volume)

  -- or maybe just the volume...
  -- print(volume.volume)

  Core.quit()
end)
