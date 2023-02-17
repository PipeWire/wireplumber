-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of common Lua test utility functions
local cu = require ("common-utils")

local u = {}

u.nodes = {}
u.nodes_to_be_destoryed = {}
u.lnkbls = {}
u.lnkbl_count = 0
u.device_count = 0
u.event_source = nil
function pushNodeRemoved (node)
  if u.event_source then
    local e = u.event_source:call ("create-event", "removed", node, nil)
    EventDispatcher.push_event (e)
  else
    Log.info ("Source not available to push event")
  end
end

function u.destroyDeviceNode (name)
  if u.nodes [name] then
    pushNodeRemoved (u.nodes [name])
  else
    u.nodes_to_be_destoryed [name] = true
  end
end

priority_value = 1000

function u.createDeviceNode (name, media_class, priority)
  local properties = {}
  properties ["node.name"] = name
  properties ["media.class"] = media_class
  properties ["priority.session"] = priority or priority_value
  priority_value = priority_value + 1
  if media_class == "Audio/Sink" then
    properties ["factory.name"] = "support.null-audio-sink"
  elseif media_class == "Audio/Source" then
    properties ["factory.name"] = "audiotestsrc"
  elseif media_class == "Video/Source" then
    properties ["factory.name"] = "videotestsrc"
  end

  node = Node ("adapter", properties)
  node:activate(Features.ALL)
  u.device_count = u.device_count + 1
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
    local prio = lnkbl.properties ["priority.session"]

    Log.info (lnkbl,
      string.format ("activated linkable:(%s) media class(%s) priority(%s)",
              name, mc, (prio and prio or nil)))

    u.lnkbls [name] = lnkbl
    u.lnkbl_count = u.lnkbl_count + 1
    -- select "default-device-node" as default device.
    if name == "default-device-node" then
      local key = nil

      if mc == "Audio/Sink" then
        key = "default.configured.audio.sink"
      elseif mc == "Audio/Source" then
        key = "default.configured.audio.source"
      end

      -- configure default device.
      u.default_metadata:set (0, key, "Spa:String:JSON", Json.Object { ["name"] = name }:get_data ())
    end
  end
}:register ()

-- hook to keep track of the nodes.
  SimpleEventHook {
    name = "node-added-removed@test-utils",
    interests = {
        -- on linkable added or removed, where linkable is adapter or plain node
        EventInterest {
            Constraint { "event.type", "c", "node-added", "node-removed" },
        },
    },
    execute = function(event)
      local node = event:get_subject()
      local name = node.properties ["node.name"]
      local props = event:get_properties()
      local event_type = props ["event.type"]

      u.event_source = u.event_source or event:get_source()

      if event_type == "node-added" then
        u.nodes [node.id] = name
        u.nodes [name] = node
    if u.nodes_to_be_destoryed [name] then
        pushNodeRemoved (u.nodes [name])
        u.nodes_to_be_destoryed [name] = nil
    end
      elseif event_type == "node-removed" then
        u.nodes [u.nodes [node.id]] = nil
      end
    end
}:register()

u.script_tester_plugin = Plugin.find ("script-tester")

function u.createStreamNode (stream_type, props)
  u.script_tester_plugin:call ("create-stream", stream_type, props)

  u.lnkbls ["stream-node"] = nil
  u.device_count = u.device_count + 1
end

function u.restartPlugin (name)
  u.script_tester_plugin:call ("restart-plugin", name)
end

u.default_metadata = cu.metadata_om:lookup {
  Constraint { "metadata.name", "=", "default" },
}
assert (u.default_metadata ~= nil)

u.settings_metadata = cu.metadata_om:lookup {
  Constraint { "metadata.name", "=", "sm-settings" },
}
assert (u.settings_metadata ~= nil)

-- update the defined target for stream session item in metadata.
function u.setTargetInMetadata (prop, target_node_name)
  u.default_metadata:set (u.lnkbls ["stream-node"].properties ["node.id"], prop,
      "Spa:Id", u.lnkbls [target_node_name].properties ["node.id"])
end

function u.linkablesReady ()
  return u.device_count == u.lnkbl_count
end

function u.updateSetting (setting, value)
  u.settings_metadata:set (0, setting, "Spa:String:JSON", value)
end

function u.clearDefaultNodeState ()
  -- clear the default-nodes state table so that it doesn't interfere with
  -- the logic of rest of the test cases, every test script builds its own
  -- table if it needs one.
  state = State ("default-nodes")
  state_table = state:load ()
  if state_table then
    state:clear (state_table)
  end
end

u.dcn_keys = {
  ["Audio/Sink"] = "default.configured.audio.sink",
  ["Audio/Source"] = "default.configured.audio.source",
  ["Video/Source"] = "default.configured.video.source",
}
return u
