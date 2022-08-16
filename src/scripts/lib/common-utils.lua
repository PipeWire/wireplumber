-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of common Lua utility functions

local cutils = {}

function cutils.parseBool (var)
  return var and (var:lower () == "true" or var == "1")
end

function cutils.getTargetDirection (properties)
  local target_direction = nil
  if properties ["item.node.direction"] == "output" or
      (properties ["item.node.direction"] == "input" and
          cutils.parseBool (properties ["stream.capture.sink"])) then
    target_direction = "input"
  else
    target_direction = "output"
  end
  return target_direction
end

function cutils.getDefaultNode (properties, target_direction)
  local target_media_class =
  properties ["media.type"] ..
      (target_direction == "input" and "/Sink" or "/Source")
  return default_nodes:call ("get-default-node", target_media_class)
end

default_nodes = Plugin.find ("default-nodes-api")

return cutils