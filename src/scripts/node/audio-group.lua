-- WirePlumber

-- Copyright © 2024 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>

-- SPDX-License-Identifier: MIT

-- audio-group.lua script takes pipewire audio stream nodes and groups them
-- into a single unit by creating a loopback filter per group. The grouping
-- is done by a common ancestor 'audio-group-namespace' process name.

agutils = require ("audio-group-utils")

PW_AUDIO_NAMESPACE = "pw-audio-namespace"

node_directions = {}
group_loopback_modules = {}
group_loopback_modules["input"] = {}
group_loopback_modules["output"] = {}

function GetNodeDirection (id, props)
  if string.find (props["media.class"], "Stream/Input/Audio") then
    return "input"
  elseif string.find (props["media.class"], "Stream/Output/Audio") then
    return "output"
  end

  return nil
end

function GetNodeAudioGroup (pid)
  local group = nil
  local target_object = nil

  -- We group a processes by PW_AUDIO_NAMESPACE.<pid> ancestor
  local curr_pid = pid
  while curr_pid ~= 0 do
    local pid_info = ProcUtils.get_proc_info (curr_pid)
    local arg0 = pid_info:get_arg (0)

    -- Check if ancestor process name is PW_AUDIO_NAMESPACE
    if arg0 ~= nil and string.find (arg0, PW_AUDIO_NAMESPACE, 1, true) then
      -- Check if the PW_AUDIO_NAMESPACE has a defined target
      for i = 0, pid_info:get_n_args () - 1, 1 do
        local argn = pid_info:get_arg (i)

        -- Ignore any args after '--'
        if argn == "--" then
          break
        end

        -- Get target node id value if any
        if (argn == "--target-object") or (argn == "-t") then
          target_object = pid_info:get_arg (i + 1)
          break
        end
      end

      -- We name the audio group as PW_AUDIO_NAMESPACE.<pid>
      group = PW_AUDIO_NAMESPACE .. "." .. tostring(curr_pid)
      break
    end

    curr_pid = pid_info:get_parent_pid ()
  end

  return group, target_object
end

function CreateStreamLoopback (props, group, target_object, direction)
  local is_input = direction == "input" and true or false

  -- Set stream properties
  local stream_props = {}
  stream_props["node.name"] = "stream.audio_group:" .. group
  stream_props["node.description"] = "Stream Audio Group for " .. group
  stream_props["media.class"] = is_input and "Stream/Input/Audio" or "Stream/Output/Audio"
  stream_props["node.passive"] = true
  stream_props["session.audio-group"] = group
  if target_object ~= nil then
    stream_props["target.object"] = tostring (target_object)
  end

  -- Set device properties
  local device_props = {}
  device_props["node.name"] = "device.audio_group:" .. group
  device_props["node.description"] = "Device Audio Group for " .. group
  device_props["media.class"] = is_input and "Audio/Source" or "Audio/Sink"
  device_props["session.audio-group"] = group

  -- Set loopback module args
  local args = Json.Object {
    ["capture.props"] = Json.Object (is_input and stream_props or device_props),
    ["playback.props"] = Json.Object (is_input and device_props or stream_props)
  }

  -- Create module
  return LocalModule("libpipewire-module-loopback", args:get_data(), {})
end

SimpleEventHook {
  name = "lib/audio-group-utils/create-audio-group-loopback",
  interests = {
    -- on linkable added or removed, where linkable is adapter or plain node
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "#", "Stream/*Audio*", type = "pw-global" },
      Constraint { "stream.monitor", "!", "true", type = "pw" },
      Constraint { "node.link-group", "-" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local source = event:get_source ()
    local client_om = source:call ("get-object-manager", "client")
    local id = node.id
    local bound_id = node["bound-id"]
    local stream_props = node.properties
    local stream_name = stream_props["node.name"]

    -- Get client
    local client = client_om:lookup {
        Constraint {  "bound-id", "=", stream_props["client.id"], type = "gobject"}
    }
    if client == nil then
      Log.info (node,
          "Cannot get client, not grouping audio stream ".. stream_name)
      return
    end

    -- Get process ID
    local pid = tonumber (client.properties ["application.process.id"])
    if pid == nil then
      Log.info (node,
          "Cannot get process ID, not grouping audio stream ".. stream_name)
      return
    end

    -- Get direction and add it to the table
    local direction = GetNodeDirection (bound_id, stream_props)
    if direction == nil then
      Log.info (node,
          "Cannot get direction, not grouping audio stream ".. stream_name)
      return
    end
    node_directions [id] = direction

    -- Get group and add it to the table
    local group, target_object = GetNodeAudioGroup (pid)
    if group == nil then
      Log.info (node,
          "Cannot get audio group, not grouping audio stream " .. stream_name)
      return
    end
    agutils.set_audio_group (node, group)

    -- Create group loopback module if it does not exist
    local m = group_loopback_modules [direction][group]
    if m == nil then
      Log.warning ("Creating " .. direction .. " loopback for audio group " .. group ..
          (target_object and (" with target object " .. tostring (target_object)) or ""))
      m = CreateStreamLoopback (stream_props, group, target_object, direction)
      group_loopback_modules [direction][group] = m
    end
  end
}:register ()


SimpleEventHook {
  name = "lib/audio-group-utils/destroy-audio-group-loopback",
  interests = {
    -- on linkable added or removed, where linkable is adapter or plain node
    EventInterest {
      Constraint { "event.type", "=", "node-removed" },
      Constraint { "media.class", "#", "Stream/*Audio*", type = "pw-global" },
      Constraint { "stream.monitor", "!", "true", type = "pw" },
      Constraint { "node.link-group", "-" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local id = node.id

    -- Get node direction from table and remove it
    local direction = node_directions [id]
    if direction == nil then
      return
    end
    node_directions [id] = nil

    -- Get node group from table and remove it
    local group = agutils.get_audio_group (node)
    if group == nil then
      return
    end
    agutils.set_audio_group (node, nil)

    -- Destroy group loopback module if there are no more nodes with the same group
    if not agutils.contains_audio_group (group) then
      Log.info ("Destroying " .. direction .. " loopback for audio group " .. group)
      group_loopback_modules [direction][group] = nil
    end
  end
}:register ()
