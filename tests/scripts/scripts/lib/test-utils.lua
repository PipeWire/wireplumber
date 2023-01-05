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

u.metadata = cu.default_metadata_om:lookup ()
assert (u.metadata ~= nil)

-- hook to keep track of the linkables created.
SimpleEventHook {
  name = "test-utils-linking",
  interests = {
    -- on linkable added or removed, where linkable is adapter or plain node
    EventInterest {
      Constraint { "event.type", "=", "session-item-added" },
      Constraint { "event.session-item.interface", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    },
  },
  execute = function (event)
    local lnkbl = event:get_subject ()
    local lp = lnkbl.properties
    local name = lp ["node.name"]

    Log.info (lnkbl, "activated linkable: " .. name ..
        " with media_class: " .. lp ["media.class"])
    if not u.lnkbls [name] then
      u.lnkbls [name] = lnkbl
    else
      Log.info ("unknown linkable " .. name)
    end

    if name == "default-device-node" then
      local args = { ["name"] = name }
      local args_json = Json.Object (args)
      local key = nil

      if lp ["media.class"] == "Audio/Sink" then
        key = "default.configured.audio.sink"
      elseif lp ["media.class"] == "Audio/Source" then
        key = "default.configured.audio.source"
      end

      -- configure default device.
      u.metadata:set (0, key, "Spa:String:JSON", args_json:get_data ())
    end
  end
}:register ()

return u
