-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- Based on restore-stream.c from pipewire-media-session
-- Copyright © 2020 Wim Taymans
--
-- SPDX-License-Identifier: MIT

-- Receive script arguments from config.lua

-- Script saves(to state file) stream properties(volume, mute, channelvolumes &
-- channel map) and if the stream happens to change "target.node" in default
-- metadata, that is also saved and when stream apears afresh, all these saved
-- properties are restored. These properties are by default remmembered
-- across(presistancy) the reboots.

-- settings file: stream.conf

local cutils = require ("common-utils")

local defaults = {}
defaults.restore_props = true
defaults.restore_target = true

local config = {}
config.restore_props = Settings.parse_boolean_safe (
    "stream.restore-props", defaults.restore_props)
config.restore_target = Settings.parse_boolean_safe (
    "stream.restore-target", defaults.restore_target)

-- the state storage
state = State ("restore-stream")
state_table = state:load ()

function formKeyBase (properties)
  local keys = {
    "media.role",
    "application.id",
    "application.name",
    "media.name",
    "node.name",
  }
  local key_base = nil

  for _, k in ipairs (keys) do
    local p = properties [k]
    if p then
      key_base = string.format ("%s:%s:%s",
          properties ["media.class"]:gsub ("^Stream/", ""), k, p)
      break
    end
  end
  return key_base
end

function saveTarget (subject, target_key, type, value)
  if target_key ~= "target.node" and target_key ~= "target.object" then
    return
  end

  local node = streams_om:lookup {
    Constraint { "bound-id", "=", subject, type = "gobject" }
  }
  if not node then
    return
  end

  local stream_props = node.properties
  cutils.evaluateRulesApplyProperties (stream_props, "stream.rules")

  if stream_props ["state.restore-target"] == "false" then
    return
  end

  local key_base = formKeyBase (stream_props)
  if not key_base then
    return
  end

  local target_value = value
  local target_name = nil

  if not target_value then
    local metadata = cutils.default_metadata_om:lookup ()
    if metadata then
      target_value = metadata:find (node ["bound-id"], target_key)
    end
  end
  if target_value and target_value ~= "-1" then
    local target_node
    if target_key == "target.object" then
      target_node = allnodes_om:lookup {
        Constraint { "object.serial", "=", target_value, type = "pw-global" }
      }
    else
      target_node = allnodes_om:lookup {
        Constraint { "bound-id", "=", target_value, type = "gobject" }
      }
    end
    if target_node then
      target_name = target_node.properties ["node.name"]
    end
  end
  state_table [key_base .. ":target"] = target_name

  Log.info (node, "saving stream target for " ..
    tostring (stream_props ["node.name"]) .. " -> " .. tostring (target_name))

  cutils.storeAfterTimeout (state, state_table)
end

function restoreTarget (node, target_name)
  local target_node = allnodes_om:lookup {
    Constraint { "node.name", "=", target_name, type = "pw" }
  }

  if target_node then
    local metadata = cutils.default_metadata_om:lookup ()
    if metadata then
      metadata:set (node ["bound-id"], "target.node", "Spa:Id",
          target_node ["bound-id"])
    end
  end
end

function jsonTable (val, name)
  local tmp = ""
  local count = 0

  if name then tmp = tmp .. string.format ("%q", name) .. ": " end

  if type (val) == "table" then
    if val ["pod_type"] == "Array" then
      tmp = tmp .. "["
      for _, v in ipairs (val) do
	if count > 0 then tmp = tmp .. "," end
        tmp = tmp .. jsonTable (v)
	count = count + 1
      end
      tmp = tmp .. "]"
    else
      tmp = tmp .. "{"
      for k, v in pairs (val) do
	if count > 0 then tmp = tmp .. "," end
        tmp = tmp .. jsonTable (v, k)
	count = count + 1
      end
      tmp = tmp .. "}"
    end
  elseif type (val) == "number" then
    tmp = tmp .. tostring (val)
  elseif type (val) == "string" then
    tmp = tmp .. string.format ("%q", val)
  elseif type (val) == "boolean" then
    tmp = tmp .. (val and "true" or "false")
  else
    tmp = tmp .. "\"[type:" .. type (val) .. "]\""
  end
  return tmp
end

function moveToMetadata (key_base, metadata)
  local route_table = { }
  local count = 0

  key = "restore.stream." .. key_base
  key = string.gsub (key, ":", ".", 1);

  local str = state_table [key_base .. ":volume"]
  if str then
    route_table ["volume"] = tonumber (str)
    count = count + 1;
  end
  local str = state_table [key_base .. ":mute"]
  if str then
    route_table ["mute"] = str == "true"
    count = count + 1;
  end
  local str = state_table [key_base .. ":channelVolumes"]
  if str then
    route_table ["volumes"] = cutils.parseArray (str, tonumber, true)
    count = count + 1;
  end
  local str = state_table [key_base .. ":channelMap"]
  if str then
    route_table ["channels"] = cutils.parseArray (str, nil, true)
    count = count + 1;
  end

  if count > 0 then
    metadata:set (0, key, "Spa:String:JSON", jsonTable (route_table));
  end
end


function saveStream (node)
  local stream_props = node.properties
  cutils.evaluateRulesApplyProperties (stream_props, "stream.rules")

  if config.restore_props and stream_props ["state.restore-props"] ~= "false"
  then

    local key_base = formKeyBase (stream_props)
    if not key_base then
      return
    end

    Log.info (node, "saving stream props for " ..
        tostring (stream_props["node.name"]))

    for p in node:iterate_params ("Props") do
      local props = cutils.parseParam (p, "Props")
      if not props then
        goto skip_prop
      end

      if props.volume then
        state_table [key_base .. ":volume"] = tostring (props.volume)
      end
      if props.mute ~= nil then
        state_table [key_base .. ":mute"] = tostring (props.mute)
      end
      if props.channelVolumes then
        state_table [key_base .. ":channelVolumes"] =
            cutils.serializeArray (props.channelVolumes)
      end
      if props.channelMap then
        state_table [key_base .. ":channelMap"] =
            cutils.serializeArray (props.channelMap)
      end

      ::skip_prop::
    end

    cutils.storeAfterTimeout (state, state_table)
  end
end

function restoreStream (node)
  local stream_props = node.properties
  cutils.evaluateRulesApplyProperties (stream_props, "stream.rules")

  local key_base = formKeyBase (stream_props)
  if not key_base then
    return
  end

  if config.restore_props and stream_props ["state.restore-props"] ~= "false"
  then
    local needsRestore = false
    local props = { "Spa:Pod:Object:Param:Props", "Props" }

    local str = state_table [key_base .. ":volume"]
    needsRestore = str and true or needsRestore
    props.volume = str and tonumber (str) or nil

    local str = state_table [key_base .. ":mute"]
    needsRestore = str and true or needsRestore
    props.mute = str and (str == "true") or nil

    local str = state_table [key_base .. ":channelVolumes"]
    needsRestore = str and true or needsRestore
    props.channelVolumes = str and cutils.parseArray (str, tonumber) or nil

    local str = state_table [key_base .. ":channelMap"]
    needsRestore = str and true or needsRestore
    props.channelMap = str and cutils.parseArray (str) or nil

    -- convert arrays to Spa Pod
    if props.channelVolumes then
      table.insert (props.channelVolumes, 1, "Spa:Float")
      props.channelVolumes = Pod.Array (props.channelVolumes)
    end
    if props.channelMap then
      table.insert (props.channelMap, 1, "Spa:Enum:AudioChannel")
      props.channelMap = Pod.Array (props.channelMap)
    end

    if needsRestore then
      Log.info (node, "restore values from " .. key_base)

      local param = Pod.Object (props)
      Log.debug (param, "setting props on " .. tostring (stream_props ["node.name"]))
      node:set_param ("Props", param)
    end
  end

  if config.restore_target and stream_props["state.restore-target"] ~= "false"
  then
    local str = state_table [key_base .. ":target"]
    if str then
      restoreTarget (node, str)
    end
  end
end

local restore_target_hook_handles = nil

local function handleRestoreTargetSetting (enable)

  if (restore_target_hook_handles == nil) and (enable == true) then
    restore_target_hook_handles = {}

    -- save "targe.node" if it is present in default metadata
    restore_target_hook_handles [1] = SimpleEventHook {
      name = "metadata-added@restore-stream-save-target",
      type = "on-event",
      priority = "default-metadata-added-restore-stream",
      interests = {
        EventInterest {
          Constraint { "event.type", "=", "object-added" },
          Constraint { "event.subject.type", "=", "metadata" },
          Constraint { "metadata.name", "=", "default" },
        },
      },
      execute = function (event)
        local metadata = event:get_subject ()

        -- process existing metadata
        for s, k, t, v in metadata:iterate (Id.ANY) do
          saveTarget (s, k, t, v)
        end
      end
    }

    -- save "target.node" on metadata changes
    restore_target_hook_handles [2] = SimpleEventHook {
      name = "metadata-changed@restore-stream-save-target",
      type = "on-event",
      priority = "default-metadata-changed-restore-stream",
      interests = {
          EventInterest {
            Constraint { "event.type", "=", "object-changed" },
            Constraint { "event.subject.type", "=", "metadata" },
            Constraint { "metadata.name", "=", "default" },
            Constraint { "event.subject.key", "=", "target.node" },
          },
          EventInterest {
            Constraint { "event.type", "=", "object-changed" },
            Constraint { "event.subject.type", "=", "metadata" },
            Constraint { "metadata.name", "=", "default" },
            Constraint { "event.subject.key", "=", "target.object" },
          },
      },
      execute = function (event)
        local subject = event:get_subject ()
        local props = event:get_properties ()

        local subject_id = props ["event.subject.id"]
        local key = props ["event.subject.key"]
        local type = props ["event.subject.spa_type"]
        local value = props ["event.subject.value"]

        saveTarget (subject_id, key, type, value)
      end
    }
    restore_target_hook_handles[1]:register()
    restore_target_hook_handles[2]:register()
  elseif (restore_target_hook_handles ~= nil) and (enable == false) then
    restore_target_hook_handles [1]:remove ()
    restore_target_hook_handles [2]:remove ()
    restore_target_hook_handles = nil
  end
end

handleRestoreTargetSetting (config.restore_target)

function handleRouteSettings (subject, key, type, value)
  if type ~= "Spa:String:JSON" then
    return
  end
  if string.find(key, "^restore.stream.") == nil then
    return
  end
  if value == nil then
    return
  end
  local json = Json.Raw (value);
  if json == nil or not json:is_object () then
    return
  end

  local vparsed = json:parse ()
  local key_base = string.sub (key, string.len ("restore.stream.") + 1)
  local str;

  key_base = string.gsub (key_base, "%.", ":", 1);

  if vparsed.volume ~= nil then
    state_table [key_base .. ":volume"] = tostring (vparsed.volume)
  end
  if vparsed.mute ~= nil then
    state_table [key_base .. ":mute"] = tostring (vparsed.mute)
  end
  if vparsed.channels ~= nil then
    state_table [key_base .. ":channelMap"] = cutils.serializeArray (vparsed.channels)
  end
  if vparsed.volumes ~= nil then
    state_table [key_base .. ":channelVolumes"] = cutils.serializeArray (vparsed.volumes)
  end

  cutils.storeAfterTimeout (state, state_table)
end


rs_metadata = ImplMetadata ("route-settings")
rs_metadata:activate (Features.ALL, function (m, e)
  if e then
    Log.warning ("failed to activate route-settings metadata: " .. tostring (e))
    return
  end

  -- copy state into the metadata
  moveToMetadata ("Output/Audio:media.role:Notification", m)
  -- watch for changes
  m:connect ("changed", function (m, subject, key, type, value)
    handleRouteSettings (subject, key, type, value)
  end)
end)

allnodes_om = ObjectManager { Interest { type = "node" } }
allnodes_om:activate ()

local restore_stream_hook_handles = nil

local function handleRestoreStreamSetting (enable)
  if (restore_stream_hook_handles == nil) and (enable == true) then
    restore_stream_hook_handles = {}

    -- restore-stream properties
    restore_stream_hook_handles [1] = SimpleEventHook {
      name = "node-added@restore-stream",
      type = "on-event",
      priority = "node-added-restore-stream",
      interests = {
        EventInterest {
          Constraint { "event.type", "=", "object-added" },
          Constraint { "event.subject.type", "=", "node" },
          Constraint { "media.class", "matches", "Stream/*", type = "pw-global" },
        },
        -- and device nodes that are not associated with any routes
        EventInterest {
          Constraint { "event.type", "=", "object-added" },
          Constraint { "event.subject.type", "=", "node" },
          Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
          Constraint { "device.routes", "is-absent", type = "pw" },
        },
        EventInterest {
          Constraint { "event.type", "=", "object-added" },
          Constraint { "event.subject.type", "=", "node" },
          Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
          Constraint { "device.routes", "equals", "0", type = "pw" },
        },
      },
      execute = function (event)
        restoreStream (event:get_subject ())
      end
    }

    -- save-stream if any of the stream parms changes
    restore_stream_hook_handles [2] = SimpleEventHook {
      name = "node-parms-changed@restore-stream-save-stream",
      type = "on-event",
      priority = "node-changed-restore-stream",
      interests = {
        EventInterest {
          Constraint { "event.type", "=", "params-changed" },
          Constraint { "event.subject.type", "=", "node" },
          Constraint { "event.subject.param-id", "=", "Props" },
          Constraint { "media.class", "matches", "Stream/*", type = "pw-global" },
        },
        -- and device nodes that are not associated with any routes
        EventInterest {
          Constraint { "event.type", "=", "params-changed" },
          Constraint { "event.subject.type", "=", "node" },
          Constraint { "event.subject.param-id", "=", "Props" },
          Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
          Constraint { "device.routes", "is-absent", type = "pw" },
        },
        EventInterest {
          Constraint { "event.type", "=", "params-changed" },
          Constraint { "event.subject.type", "=", "node" },
          Constraint { "event.subject.param-id", "=", "Props" },
          Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
          Constraint { "device.routes", "equals", "0", type = "pw" },
        },
      },
      execute = function (event)
        saveStream (event:get_subject())
      end
    }
    restore_stream_hook_handles[1]:register()
    restore_stream_hook_handles[2]:register()


  elseif (restore_stream_hook_handles ~= nil) and (enable == false) then
    restore_stream_hook_handles [1]:remove ()
    restore_stream_hook_handles [2]:remove ()
    restore_stream_hook_handles = nil
  end
end

handleRestoreStreamSetting (config.restore_props)

local function settingsChangedCallback (_, setting, _)

  if setting == "stream.restore-props" then
    config.restore_props = Settings.parse_boolean_safe ("stream.restore-props",
        config.restore_props)
    handleRestoreStreamSetting (config.restore_props)
  elseif setting == "stream.restore-target" then
    config.restore_target = Settings.parse_boolean_safe ("stream.restore-target",
        config.restore_target)
    handleRestoreTargetSetting (config.restore_target)
  end
end

Settings.subscribe ("stream*", settingsChangedCallback)

streams_om = ObjectManager {
  -- match stream nodes
  Interest {
    type = "node",
    Constraint { "media.class", "matches", "Stream/*", type = "pw-global" },
  },
  -- and device nodes that are not associated with any routes
  Interest {
    type = "node",
    Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
    Constraint { "device.routes", "is-absent", type = "pw" },
  },
  Interest {
    type = "node",
    Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
    Constraint { "device.routes", "equals", "0", type = "pw" },
  },
}

streams_om:activate ()
