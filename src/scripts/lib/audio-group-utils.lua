-- WirePlumber

-- Copyright © 2024 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of audio group Lua utility functions

local module = {
  node_groups = {},
}

function module.set_audio_group (stream_node, audio_group)
  module.node_groups [stream_node.id] = audio_group
end

function module.get_audio_group (stream_node)
  return module.node_groups [stream_node.id]
end

function module.contains_audio_group (audio_group)
  for k, v in pairs(module.node_groups) do
    if v == group then
      return true
    end
  end
  return false
end

return module
