-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of common Lua test utility functions
local cu = require ("common-utils")

local u = {}

u.nodes = {}
u.lnkbls = {}
u.lnkbl_count = 0

function u.createDeviceNode (name, media_class)
  local properties = {}
  properties ["node.name"] = name
  properties ["media.class"] = media_class
  if media_class == "Audio/Sink" then
    properties ["factory.name"] = "support.null-audio-sink"
  else
    properties ["factory.name"] = "audiotestsrc"
  end

  node = Node ("adapter", properties)
  node:activate (Feature.Proxy.BOUND, function (n)
    local name = n.properties ["node.name"]
    local mc = n.properties ["media.class"]
    Log.info (n, "created and activated device node: " .. name)
    u.nodes [name] = n

    -- wait for linkables to be created.
    u.lnkbls [name] = nil
    u.lnkbl_count = u.lnkbl_count + 1
  end)

  return node
end

function u.createStreamNode (name)
  -- stream node not created in Lua but in C in the test launcher
  u.lnkbls ["stream-node"] = nil
  u.lnkbl_count = u.lnkbl_count + 1
end

return u
