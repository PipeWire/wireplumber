-- WirePlumber

-- Copyright © 2024 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>

-- SPDX-License-Identifier: MIT

-- audio-group.lua script takes pipewire audio streams and groups them
-- into a single unit by creating a loopback filter per group. The grouping
-- is done by a common ancestor 'audio-group-namespace' process name.

agutils = require ("audio-group-utils")

PW_AUDIO_NAMESPACE = "pw-audio-namespace"

stream_directions = {}
group_loopback_modules = {}
group_loopback_modules[Direction.INPUT] = {}
group_loopback_modules[Direction.OUTPUT] = {}

function getDirectionString (direction)
  return direction == Direction.INPUT and "input" or "output"
end

function GetStreamAudioGroup (pid)
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
  local is_input = direction == Direction.INPUT

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
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "kind", "=", StreamKind.SUBJECT, type = "gobject" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local source = event:get_source ()
    local client_om = source:call ("get-object-manager", "client")
    local id = stream.id
    local stream_props = stream:get_properties ()
    local stream_name = stream_props["node.name"]

    -- Only handle plain streams, not monitors or streams that are already
    -- part of a link group (e.g. filters)
    if stream_props:get_boolean ("stream.monitor") or
        stream_props["node.link-group"] ~= nil then
      return
    end

    -- Get client
    local client = client_om:lookup {
        Constraint {  "bound-id", "=", stream_props["client.id"], type = "gobject"}
    }
    if client == nil then
      Log.info (stream,
          "Cannot get client, not grouping audio stream ".. stream_name)
      return
    end

    -- Get process ID
    local pid = tonumber (client.properties ["application.process.id"])
    if pid == nil then
      Log.info (stream,
          "Cannot get process ID, not grouping audio stream ".. stream_name)
      return
    end

    -- Get direction and add it to the table
    local direction = stream:get_direction ()
    stream_directions [id] = direction

    -- Get group and add it to the table
    local group, target_object = GetStreamAudioGroup (pid)
    if group == nil then
      Log.info (stream,
          "Cannot get audio group, not grouping audio stream " .. stream_name)
      return
    end
    agutils.set_audio_group (stream, group)

    -- Create group loopback module if it does not exist
    local m = group_loopback_modules [direction][group]
    if m == nil then
      Log.info ("Creating " .. getDirectionString (direction) ..
          " loopback for audio group " .. group ..
          (target_object and (" with target object " .. tostring (target_object)) or ""))
      m = CreateStreamLoopback (stream_props, group, target_object, direction)
      group_loopback_modules [direction][group] = m
    end
  end
}:register ()


SimpleEventHook {
  name = "lib/audio-group-utils/destroy-audio-group-loopback",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-removed" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "kind", "=", StreamKind.SUBJECT, type = "gobject" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local id = stream.id

    -- Get stream direction from table and remove it
    local direction = stream_directions [id]
    if direction == nil then
      return
    end
    stream_directions [id] = nil

    -- Get stream group from table and remove it
    local group = agutils.get_audio_group (stream)
    if group == nil then
      return
    end
    agutils.set_audio_group (stream, nil)

    -- Destroy group loopback module if there are no more streams with the same group
    if not agutils.contains_audio_group (group) then
      Log.info ("Destroying " .. getDirectionString (direction) ..
          " loopback for audio group " .. group)
      group_loopback_modules [direction][group] = nil
    end
  end
}:register ()
