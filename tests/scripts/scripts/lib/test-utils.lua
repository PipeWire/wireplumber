-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of common Lua test utility functions

local u = {}

u.nodes = {}
u.streams = {}
u.stream_count = 0

-- Calls cb once condition() holds. Fails the test if it never does.
function u.waitUntil (condition, cb)
  local attempts = 0

  Core.timeout_add (10, function ()
    if condition () then
      cb ()
      return false
    end
    attempts = attempts + 1
    assert (attempts < 500)
    return true
  end)
end

function u.createDeviceNode (name, media_class, props)
  local properties = {}
  properties ["node.name"] = name
  properties ["media.class"] = media_class
  if media_class == "Audio/Sink" then
    properties ["factory.name"] = "support.null-audio-sink"
  else
    properties ["factory.name"] = "audiotestsrc"
  end
  if props ~= nil then
    for k, v in pairs (props) do
      properties[k] = v
    end
  end

  node = Node ("adapter", properties)
  node:activate (Features.ALL, function (n)
    local name = n.properties ["node.name"]
    Log.info (n, "created and activated device node: " .. name)
    u.nodes [name] = n

    -- wait for the corresponding stream to be created
    u.streams [name] = nil
    u.stream_count = u.stream_count + 1
  end)
  return node
end

-- hook to keep track of the streams created.
-- the linking hooks must see the streams table already updated
SimpleEventHook {
  name = "stream-added@test-utils-linking",
  before = "linking/stream-added-immediate",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local stream_props = stream:get_properties ()
    local name = stream_props ["node.name"]
    local mc = stream_props ["media.class"]

    Log.info (stream, "activated stream: " .. name .. " with " .. mc)

    u.streams [name] = stream

    -- select "default-device-node" as default device.
    if name == "default-device-node" then
      local key = nil

      if mc == "Audio/Sink" then
        key = "default.configured.audio.sink"
      elseif mc == "Audio/Source" then
        key = "default.configured.audio.source"
      end

      -- configure default device.
      u.default_metadata:set(0, key, "Spa:String:JSON", Json.Object { ["name"] = name }:get_data())
    end
  end
}:register ()

u.script_tester_plugin = Plugin.find ("script-tester")

function u.createStreamNode (stream_type, props)
  u.script_tester_plugin:call ("create-stream", stream_type, props)

  local name = props and props ["node.name"] or "stream-node"
  u.streams [name] = nil
  u.stream_count = u.stream_count + 1
end

-- The error message the daemon last sent to the client stream with this
-- node.name, or nil if it never got one.
function u.getStreamError (name)
  return u.script_tester_plugin:call ("get-stream-error", name)
end

-- Whether the client stream with this node.name has a format set, which
-- happens only when its link has negotiated one.
function u.streamHasFormat (name)
  return u.script_tester_plugin:call ("get-stream-has-format", name)
end

function u.restartPlugin (name)
  u.script_tester_plugin:call ("restart-plugin", name)
end

function u.destroyStreamNodes ()
  u.script_tester_plugin:call ("destroy-streams")
end

-- connects another client to PipeWire, with the given properties
function u.connectClient (props)
  u.script_tester_plugin:call ("connect-client", props)
end

-- disconnects the most recently connected client
function u.disconnectClient ()
  u.script_tester_plugin:call ("disconnect-client")
end

local metadata_om = Plugin.find ("standard-event-source"):call (
    "get-object-manager", "metadata")

u.default_metadata = metadata_om:lookup {
  Constraint { "metadata.name", "=", "default" },
}
assert (u.default_metadata ~= nil)

u.settings_metadata = metadata_om:lookup {
  Constraint { "metadata.name", "=", "sm-settings" },
}
assert (u.settings_metadata ~= nil)

-- Changes a setting and calls cb once the change has reached WpSettings.
-- The value is the setting's JSON, so "true" for a boolean, not true.
function u.setSetting (name, value, cb)
  u.settings_metadata:set (0, name, "Spa:String:JSON", value)

  u.waitUntil (function ()
    local val = Settings.get (name)
    return val ~= nil and val:get_data () == value
  end, cb)
end

-- Update the defined target for a stream in metadata.
function u.setTargetInMetadata (prop, target_node_name, target_property)
  local stream = u.streams ["stream-node"]
  local target_stream = u.streams [target_node_name]
  local target_value = target_stream:get_properties () [target_property]
  if target_property == "node.id" then
    target_value = target_stream:get_node_bound_id ()
  end
  u.default_metadata:set (stream:get_node_bound_id (), prop,
      "Spa:Id", target_value)
end

function u.streamsReady ()
  local count = 0
  for k, v in pairs (u.streams) do
    if v then
      count = count + 1
    end
  end
  if count == u.stream_count then
    return true
  end

  return false
end

function u.getStreamLink (stream, target)
  for peer in StreamLinkManager.iterate_peers (stream) do
    if peer == target then
      local stream_link = StreamLinkManager.find_link (stream, target)
      if stream:get_direction () == Direction.INPUT then
        stream_link = StreamLinkManager.find_link (target, stream)
      end
      return stream_link
    end
  end
  return nil
end

return u
