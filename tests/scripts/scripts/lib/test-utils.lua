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
  node:activate (Features.ALL, function (n)
    local name = n.properties ["node.name"]
    Log.info (n, "created and activated device node: " .. name)
    u.nodes [name] = n

    -- wait for linkables to be created.
    u.lnkbls [name] = nil
    u.lnkbl_count = u.lnkbl_count + 1
  end)
  return node
end

-- hook to keep track of the linkables created.
SimpleEventHook {
  name = "linkable-added@test-utils-linking",
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
    local name = lnkbl.properties ["node.name"]
    local mc = lnkbl.properties ["media.class"]

    Log.info (lnkbl, "activated linkable: " .. name .. " with " .. mc)

    u.lnkbls [name] = lnkbl

    -- select "default-device-node" as default device.
    if name == "default-device-node" then
      local key = nil

      if mc == "Audio/Sink" then
        key = "default.configured.audio.sink"
      elseif mc == "Audio/Source" then
        key = "default.configured.audio.source"
      end

      -- configure default device.
      u.metadata:set (0, key, "Spa:String:JSON", Json.Object { ["name"] = name }:get_data ())
    end
  end
}:register ()

u.script_tester_plugin = Plugin.find ("script-tester")

function u.createStreamNode (stream_type, props)
  u.script_tester_plugin:call ("create-stream", stream_type, props)

  u.lnkbls ["stream-node"] = nil
  u.lnkbl_count = u.lnkbl_count + 1
end

function u.restartPlugin (name)
  u.script_tester_plugin:call ("restart-plugin", name)
end

u.metadata = cu.default_metadata_om:lookup ()
assert (u.metadata ~= nil)

-- update the defined target for stream session item in metadata.
function u.setTargetInMetadata (prop, target_node_name)
  u.metadata:set (u.lnkbls ["stream-node"].properties ["node.id"], prop,
      "Spa:Id", u.lnkbls [target_node_name].properties ["node.id"])
end

function u.linkablesReady ()
  local count = 0
  for k, v in pairs (u.lnkbls) do
    if v then
      count = count + 1
    end
  end
  if count == u.lnkbl_count then
    return true
  end

  return false
end

return u
