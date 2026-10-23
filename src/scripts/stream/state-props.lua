-- WirePlumber
--
-- Copyright © 2021-2022 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- Based on restore-stream.c from pipewire-media-session
-- Copyright © 2020 Wim Taymans
--
-- SPDX-License-Identifier: MIT
--
-- Stores the Props (volumes, mute and channel map) of each audio stream and
-- restores them when the stream appears again.

cutils = require ("common-utils")
log = Log.open_topic ("s-stream")

config = {}
config.rules = Conf.get_section_as_json ("stream.rules", Json.Array {})

-- the state storage
state = nil
state_table = nil

-- Support for the "System Sounds" volume control in pavucontrol
rs_metadata = nil

function numbersEqualRounded (a, b, decimals)
  if a == b then
    return true
  end
  if type (a) ~= "number" or type (b) ~= "number" then
    return false
  end

  local scale = 10 ^ (decimals or 0)
  local a_rounded = math.modf (a * scale)
  local b_rounded = math.modf (b * scale)
  return a_rounded == b_rounded
end

function isNonEmptyArray (value)
  return type (value) == "table" and #value > 0
end

function arraysEqual (a, b)
  if a == b then
    return true
  end
  if type (a) ~= "table" or type (b) ~= "table" then
    return false
  end
  if #a ~= #b then
    return false
  end
  for i = 1, #a do
    if a [i] ~= b [i] then
      return false
    end
  end
  return true
end

function arraysEqualRounded (a, b, decimals)
  if a == b then
    return true
  end
  if type (a) ~= "table" or type (b) ~= "table" then
    return false
  end
  if #a ~= #b then
    return false
  end
  for i = 1, #a do
    if not numbersEqualRounded (a [i], b [i], decimals) then
      return false
    end
  end
  return true
end

-- Returns the stream properties updated with the stream rules
function getStreamProps (stream)
  local stream_props = stream:get_properties ():copy ()
  return JsonUtils.match_rules_update_properties (config.rules, stream_props)
end

-- hook to restore the stream Props
restore_stream_props_hook = SimpleEventHook {
  name = "stream/restore-stream-props",
  interests = {
    -- match the audio streams of client nodes
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "kind", "=", StreamKind.SUBJECT, type = "gobject" },
    },
    -- and of audio streams of device nodes that are not associated with any routes
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "kind", "=", StreamKind.TARGET, type = "gobject" },
      Constraint { "device.routes", "is-absent" },
    },
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "kind", "=", StreamKind.TARGET, type = "gobject" },
      Constraint { "device.routes", "=", "0" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local stream_props = getStreamProps (stream)

    if stream_props ["state.restore-props"] == "false" then
      return
    end

    local key = formKey (stream, stream_props)
    if not key then
      return
    end

    local stored_values = getStoredStreamProps (key) or {}
    local channelVolumes = stored_values.channelVolumes or
        buildDefaultChannelVolumes (stream, stream_props)

    local props = {}
    if stored_values.volume then
      props.volume = Pod.Float (stored_values.volume)
    end
    if stored_values.mute ~= nil then
      props.mute = Pod.Boolean (stored_values.mute)
    end
    if channelVolumes then
      table.insert (channelVolumes, 1, "Spa:Float")
      props.channelVolumes = Pod.Array (channelVolumes)
    end
    if stored_values.channelMap then
      local channelMap = stored_values.channelMap
      table.insert (channelMap, 1, "Spa:Enum:AudioChannel")
      props.channelMap = Pod.Array (channelMap)
    end

    if next (props) then
      log:info (stream, "restore values from " .. key)
      stream:set_props_param (props)
    end
  end
}

-- store the stream Props on the state file
store_stream_props_hook = SimpleEventHook {
  name = "stream/store-stream-props",
  interests = {
    -- match the audio streams of client nodes
    EventInterest {
      Constraint { "event.type", "=", "stream-params-changed" },
      Constraint { "event.subject.param-id", "=", "Props" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "kind", "=", StreamKind.SUBJECT, type = "gobject" },
    },
    -- and of device nodes that are not associated with any routes
    EventInterest {
      Constraint { "event.type", "=", "stream-params-changed" },
      Constraint { "event.subject.param-id", "=", "Props" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "kind", "=", StreamKind.TARGET, type = "gobject" },
      Constraint { "device.routes", "is-absent" },
    },
    EventInterest {
      Constraint { "event.type", "=", "stream-params-changed" },
      Constraint { "event.subject.param-id", "=", "Props" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "kind", "=", StreamKind.TARGET, type = "gobject" },
      Constraint { "device.routes", "=", "0" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local stream_props = getStreamProps (stream)

    if stream_props ["state.restore-props"] == "false" then
      return
    end

    local key = formKey (stream, stream_props)
    if not key then
      return
    end

    local stored_values = getStoredStreamProps (key) or {}
    local hasChanges = false

    for p in stream:iterate_params ("Props") do
      local obj = p:parse ()
      if obj.pod_type ~= "Object" or obj.object_id ~= "Props" then
        goto skip_prop
      end
      local props = obj.properties

      if props.volume ~= nil and
          not numbersEqualRounded (props.volume, stored_values.volume, 4) then
        stored_values.volume = props.volume
        hasChanges = true
      end
      if props.mute ~= nil and props.mute ~= stored_values.mute then
        stored_values.mute = props.mute
        hasChanges = true
      end
      if isNonEmptyArray (props.channelVolumes) and
          not arraysEqualRounded (props.channelVolumes,
              stored_values.channelVolumes, 4) then
        stored_values.channelVolumes = props.channelVolumes
        hasChanges = true
      end
      if isNonEmptyArray (props.channelMap) and
          not arraysEqual (props.channelMap, stored_values.channelMap) then
        stored_values.channelMap = props.channelMap
        hasChanges = true
      end

      ::skip_prop::
    end

    if hasChanges then
      log:info (stream, "saving stream props for " .. key)
      saveStreamProps (key, stored_values)
    end
  end
}

-- populate route-settings metadata
function populateMetadata (metadata)
  -- copy state into the metadata
  local key = "Output/Audio:media.role:Notification"
  local p = getStoredStreamProps (key)
  if p then
    p.channels = p.channelMap and Json.Array (p.channelMap)
    p.volumes = p.channelVolumes and Json.Array (p.channelVolumes)
    p.channelMap = nil
    p.channelVolumes = nil

    -- pipewire-pulse expects the key to be
    -- "restore.stream.Output/Audio.media.role:Notification"
    key = string.gsub (key, ":", ".", 1);
    metadata:set (0, "restore.stream." .. key, "Spa:String:JSON",
        Json.Object (p):to_string ())
  end
end

-- track route-settings metadata changes
route_settings_metadata_changed_hook = SimpleEventHook {
  name = "stream/route-settings-metadata-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "route-settings" },
      Constraint { "event.subject.key", "=",
          "restore.stream.Output/Audio.media.role:Notification" },
      Constraint { "event.subject.spa_type", "=", "Spa:String:JSON" },
      Constraint { "event.subject.value", "is-present" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    local key = props ["event.subject.key"]
    local value = props ["event.subject.value"]

    local json = Json.Raw (value)
    if json == nil or not json:is_object () then
      return
    end

    local vparsed = json:parse ()

    -- we store the key as "Output/Audio:media.role:Notification"
    local key = string.sub (key, string.len ("restore.stream.") + 1)
    key = string.gsub (key, "%.", ":", 1);

    local stored_values = getStoredStreamProps (key) or {}

    if vparsed.volume ~= nil then
      stored_values.volume = vparsed.volume
    end
    if vparsed.mute ~= nil then
      stored_values.mute = vparsed.mute
    end
    if vparsed.channels ~= nil then
      stored_values.channelMap = vparsed.channels
    end
    if vparsed.volumes ~= nil then
      stored_values.channelVolumes = vparsed.volumes
    end
    saveStreamProps (key, stored_values)
  end
}

-- Returns the number of channels of a raw format, or nil if it has none
function getFormatChannels (format)
  local parsed = format:parse ()
  if parsed == nil then
    return nil
  end
  local t = type (parsed.properties.channels)
  if t == "number" then
    return parsed.properties.channels
  elseif t == "table" and #parsed.properties.channels > 0 then
    -- in some misbehaving clients a non-fixed Format may appear here, which means the number of
    -- channels will be some kind of choice. If this is the case, pick the first number in the
    -- choice (which is either the default in an enum or range, or may just happen to be the
    -- right number in other cases)
    return parsed.properties.channels [1]
  end
  return nil
end

function buildDefaultChannelVolumes (stream, stream_props)
  local direction = cutils.mediaClassToDirection (stream_props ["media.class"] or "")
  local def_vol = 1.0
  local channels = nil
  local res = {}

  local str = stream_props ["state.default-volume"]
  if str ~= nil then
    def_vol = tonumber (str)
  elseif direction == "input" then
    def_vol = Settings.get_float ("node.stream.default-capture-volume")
  elseif direction == "output" then
    def_vol = Settings.get_float ("node.stream.default-playback-volume")
  end

  -- the Format of the node is the one of its only stream if the stream has no
  -- port group, otherwise use the supported formats of the stream
  local node = stream:get_node ()
  if node and stream:get_port_group () == nil then
    for pod in node:iterate_params ("Format") do
      channels = getFormatChannels (pod)
      if channels then
        break
      end
    end
  end
  if not channels then
    for pod in stream:iterate_supported_raw_formats () do
      channels = getFormatChannels (pod)
      if channels then
        break
      end
    end
  end
  channels = channels or 2

  log:info (stream, "using default volume: " .. tostring (def_vol) ..
      ", channels: " .. tostring (channels))

  while (#res < channels) do
    table.insert (res, def_vol)
  end

  return res
end

function getStoredStreamProps (key)
  local value = state_table [key]
  if not value then
    return nil
  end

  local json = Json.Raw (value)
  if not json or not json:is_object () then
    return nil
  end

  return json:parse ()
end

function saveStreamProps (key, p)
  assert (type (p) == "table")

  p.channelMap = p.channelMap and Json.Array (p.channelMap)
  p.channelVolumes = p.channelVolumes and Json.Array (p.channelVolumes)

  state_table [key] = Json.Object (p):to_string ()
  state:save_after_timeout (state_table)
end

function formKey (stream, properties)
  local keys = {
    "application.id",
    "application.name",
    "media.name",
    "node.name",
  }
  local key_base = nil

  -- Only use the media.role key if it is set to Notification, otherwise use
  -- the application keys.
  if properties ["media.role"] == "Notification" then
    key_base = string.format ("%s:%s",
        properties ["media.class"]:gsub ("^Stream/", ""), "media.role:Notification")
  else
    for _, k in ipairs (keys) do
      local p = properties [k]
      if p then
        key_base = string.format ("%s:%s:%s",
            properties ["media.class"]:gsub ("^Stream/", ""), k, p)
        break
      end
    end
  end

  -- each stream of a node has its own props
  local port_group = stream:get_port_group ()
  if key_base and port_group then
    key_base = key_base .. ":stream:" .. port_group
  end

  return key_base
end

function toggleState (enable)
  if enable and not state then
    state = State ("stream-props")
    state_table = state:load ()

    restore_stream_props_hook:register ()
    store_stream_props_hook:register ()
    route_settings_metadata_changed_hook:register ()

    rs_metadata = ImplMetadata ("route-settings")
    rs_metadata:activate (Features.ALL, function (m, e)
      if e then
        log:warning ("failed to activate route-settings metadata: " .. tostring (e))
      else
        populateMetadata (m)
      end
    end)

  elseif not enable and state then
    state = nil
    state_table = nil
    restore_stream_props_hook:remove ()
    store_stream_props_hook:remove ()
    route_settings_metadata_changed_hook:remove ()
    rs_metadata = nil
  end
end

Settings.subscribe ("node.stream.restore-props", function ()
  toggleState (Settings.get_boolean ("node.stream.restore-props"))
end)
toggleState (Settings.get_boolean ("node.stream.restore-props"))
