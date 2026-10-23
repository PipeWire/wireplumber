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
-- Stores the target of each stream and restores it when the stream appears
-- again, using the "target.stream.<port-group>" default metadata key. Streams
-- without a port group use the "target.object" key of their node instead.

log = Log.open_topic ("s-stream")

config = {}
config.rules = Conf.get_section_as_json ("stream.rules", Json.Array {})

-- the state storage
state = nil
state_table = nil

-- Returns the target object and the optional target port group of a
-- "target.stream" JSON value, or nil if the target object is missing
function parseTargetStream (value)
  if value == nil then
    return nil
  end
  local json = Json.Raw (value)
  if not json:is_object () then
    return nil
  end
  local t = json:parse ()
  if t ["target.object"] == nil then
    return nil
  end
  local group = t ["target.stream"]
  return tostring (t ["target.object"]), group and tostring (group)
end

-- Returns the stream properties updated with the stream rules
function getStreamProps (stream)
  local stream_props = stream:get_properties ():copy ()
  return JsonUtils.match_rules_update_properties (config.rules, stream_props)
end

-- hook to restore the stream target
restore_stream_target_hook = SimpleEventHook {
  name = "stream/restore-stream-target",
  interests = {
    -- match the streams of client nodes
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "kind", "=", StreamKind.SUBJECT, type = "gobject" },
    },
    -- and of device nodes that are not associated with any routes
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "kind", "=", StreamKind.TARGET, type = "gobject" },
      Constraint { "device.routes", "is-absent" },
    },
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "kind", "=", StreamKind.TARGET, type = "gobject" },
      Constraint { "device.routes", "equals", "0" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local stream_props = getStreamProps (stream)

    if stream_props ["state.restore-target"] == "false" then
      return
    end

    local key = formKey (stream, stream_props)
    if not key then
      return
    end

    local stored_values = getStoredStreamTarget (key)
    if not stored_values or not stored_values.target then
      return
    end

    -- check first if there is a defined target in the stream's properties
    -- and skip restoring if this is the case (#335)
    local target_in_props = stream_props ["target.stream"] or
        stream_props ["target.object"] or stream_props ["node.target"]
    if target_in_props then
      log:debug (stream,
          "Not restoring the target for " ..
          tostring (stream_props ["node.name"]) ..
          " because it is already set to " .. target_in_props)
      return
    end

    local source = event:get_source ()
    local streams_om = source:call ("get-object-manager", "stream")
    local metadata_om = source:call ("get-object-manager", "metadata")

    local target_serial = nil
    for candidate in streams_om:iterate { type = "Stream" } do
      if candidate:get_property ("node.name") == stored_values.target and
          (stored_values.stream == nil or
              candidate:get_port_group () == stored_values.stream) then
        target_serial = candidate:get_property ("object.serial")
        break
      end
    end
    local metadata = metadata_om:lookup {
      Constraint { "metadata.name", "=", "default" }
    }
    if not target_serial or not metadata then
      return
    end

    log:info (stream, "restore target from " .. key)

    local node_id = stream:get_node_bound_id ()
    local port_group = stream:get_port_group ()
    if port_group then
      local value = Json.Object {
        ["target.object"] = target_serial,
        ["target.stream"] = stored_values.stream,
      }
      metadata:set (node_id, "target.stream." .. port_group, "Spa:String:JSON",
          value:to_string ())
    else
      metadata:set (node_id, "target.object", "Spa:Id", target_serial)
    end
  end
}

-- save "target.stream.<port-group>", "target.object" and "target.node" on
-- metadata changes
store_stream_target_hook = SimpleEventHook {
  name = "stream/store-stream-target-metadata-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "c", "target.object", "target.node" },
    },
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "#", "target.stream.*" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local streams_om = source:call ("get-object-manager", "stream")
    local props = event:get_properties ()
    local subject_id = tonumber (props ["event.subject.id"])
    local target_key = props ["event.subject.key"]
    local target_value = props ["event.subject.value"]

    -- the node keys apply to all the streams of the node, while the
    -- "target.stream.<port-group>" key only applies to one of them
    local port_group = nil
    local target_object = nil
    local target_group = nil
    if target_key == "target.object" or target_key == "target.node" then
      target_object = target_value
    else
      port_group = target_key:sub (string.len ("target.stream.") + 1)
      target_object, target_group = parseTargetStream (target_value)
    end

    -- find the name of the target node
    local target_name = nil
    if target_object and target_object ~= "-1" then
      for candidate in streams_om:iterate { type = "Stream" } do
        local matches
        if not tonumber (target_object) then
          matches = candidate:get_property ("node.name") == target_object or
              candidate:get_property ("object.path") == target_object
        elseif target_key == "target.node" then
          matches = tostring (candidate:get_node_bound_id ()) == target_object
        else
          matches = candidate:get_property ("object.serial") == target_object
        end
        if matches then
          target_name = candidate:get_property ("node.name")
          break
        end
      end
    end

    for stream in streams_om:iterate { type = "Stream" } do
      if stream:get_node_bound_id () ~= subject_id or
          (port_group and stream:get_port_group () ~= port_group) then
        goto next_stream
      end

      local stream_props = getStreamProps (stream)
      if stream_props ["state.restore-target"] == "false" then
        goto next_stream
      end

      local key = formKey (stream, stream_props)
      if not key then
        goto next_stream
      end

      log:info (stream, "saving stream target for " ..
          tostring (stream_props ["node.name"]) .. " -> " ..
          tostring (target_name) .. ":" .. tostring (target_group))

      saveStreamTarget (key, target_name and
          { target = target_name, stream = target_group })

      ::next_stream::
    end
  end
}

function getStoredStreamTarget (key)
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

function saveStreamTarget (key, p)
  state_table [key] = p and Json.Object (p):to_string ()
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

  -- each stream of a node has its own target
  local port_group = stream:get_port_group ()
  if key_base and port_group then
    key_base = key_base .. ":stream:" .. port_group
  end

  return key_base
end

function toggleState (enable)
  if enable and not state then
    state = State ("stream-target")
    state_table = state:load ()
    restore_stream_target_hook:register ()
    store_stream_target_hook:register ()
  elseif not enable and state then
    state = nil
    state_table = nil
    restore_stream_target_hook:remove ()
    store_stream_target_hook:remove ()
  end
end

Settings.subscribe ("node.stream.restore-target", function ()
  toggleState (Settings.get_boolean ("node.stream.restore-target"))
end)
toggleState (Settings.get_boolean ("node.stream.restore-target"))
