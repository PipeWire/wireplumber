-- WirePlumber
--
-- Copyright © 2021-2022 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- Based on restore-stream.c from pipewire-media-session
-- Copyright © 2020 Wim Taymans
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
config = require ("stream-config")

-- the state storage
state = nil
state_table = nil

-- Support for the "System Sounds" volume control in pavucontrol
rs_metadata = nil

-- hook to restore stream properties & target
restore_stream_hook = SimpleEventHook {
  name = "node/restore-stream",
  interests = {
    -- match stream nodes
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "matches", "Stream/*" },
    },
    -- and device nodes that are not associated with any routes
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "matches", "Audio/*" },
      Constraint { "device.routes", "is-absent" },
    },
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "media.class", "matches", "Audio/*" },
      Constraint { "device.routes", "equals", "0" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local stream_props = node.properties
    cutils.evaluateRulesApplyProperties (stream_props, "stream.rules")

    local key = formKey (stream_props)
    if not key then
      return
    end

    local stored_values = getStoredStreamProps (key)
    if not stored_values then
      return
    end

    -- restore node Props (volumes, channelMap, etc...)
    if config.restore_props and stream_props ["state.restore-props"] ~= "false"
    then
      local props = {
        "Spa:Pod:Object:Param:Props", "Props",
        volume = stored_values.volume,
        mute = stored_values.mute,
        channelVolumes = stored_values.channelVolumes ~= nil and
            stored_values.channelVolumes or buildDefaultChannelVolumes (node),
        channelMap = stored_values.channelMap,
      }
      -- convert arrays to Spa Pod
      if props.channelVolumes then
        table.insert (props.channelVolumes, 1, "Spa:Float")
        props.channelVolumes = Pod.Array (props.channelVolumes)
      end
      if props.channelMap then
        table.insert (props.channelMap, 1, "Spa:Enum:AudioChannel")
        props.channelMap = Pod.Array (props.channelMap)
      end

      if props.volume or (props.mute ~= nil) or props.channelVolumes or props.channelMap
      then
        Log.info (node, "restore values from " .. key)

        local param = Pod.Object (props)
        Log.debug (param, "setting props on " .. tostring (stream_props ["node.name"]))
        node:set_param ("Props", param)
      end
    end

    -- restore the node's link target on metadata
    if config.restore_target and stream_props["state.restore-target"] ~= "false"
    then
      if stored_values.target then
        -- check first if there is a defined target in the node's properties
        -- and skip restoring if this is the case (#335)
        local target_in_props =
            stream_props ["target.object"] or stream_props ["node.target"]

        if not target_in_props then
          local source = event:get_source ()
          local nodes_om = source:call ("get-object-manager", "node")
          local metadata_om = source:call ("get-object-manager", "metadata")

          local target_node = nodes_om:lookup {
            Constraint { "node.name", "=", stored_values.target, type = "pw" }
          }
          local metadata = metadata_om:lookup {
            Constraint { "metadata.name", "=", "default" }
          }

          if target_node and metadata then
            metadata:set (node ["bound-id"], "target.object", "Spa:Id",
                  target_node.properties ["object.serial"])
          end
        else
          Log.debug (node,
              "Not restoring the target for " ..
              tostring (stream_props ["node.name"]) ..
              " because it is already set to " .. target_in_props)
        end
      end
    end

  end
}

-- store stream properties on the state file
store_stream_props_hook = SimpleEventHook {
  name = "node/store-stream-props",
  interests = {
    -- match stream nodes
    EventInterest {
      Constraint { "event.type", "=", "node-params-changed" },
      Constraint { "event.subject.param-id", "=", "Props" },
      Constraint { "media.class", "matches", "Stream/*" },
    },
    -- and device nodes that are not associated with any routes
    EventInterest {
      Constraint { "event.type", "=", "node-params-changed" },
      Constraint { "event.subject.param-id", "=", "Props" },
      Constraint { "media.class", "matches", "Audio/*" },
      Constraint { "device.routes", "is-absent" },
    },
    EventInterest {
      Constraint { "event.type", "=", "node-params-changed" },
      Constraint { "event.subject.param-id", "=", "Props" },
      Constraint { "media.class", "matches", "Audio/*" },
      Constraint { "device.routes", "equals", "0" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local stream_props = node.properties
    cutils.evaluateRulesApplyProperties (stream_props, "stream.rules")

    if config.restore_props and stream_props ["state.restore-props"] ~= "false"
    then
      local key = formKey (stream_props)
      if not key then
        return
      end

      local stored_values = getStoredStreamProps (key) or {}
      local hasChanges = false

      Log.info (node, "saving stream props for " ..
          tostring (stream_props ["node.name"]))

      for p in node:iterate_params ("Props") do
        local props = cutils.parseParam (p, "Props")
        if not props then
          goto skip_prop
        end

        if props.volume ~= stored_values.volume then
          stored_values.volume = props.volume
          hasChanges = true
        end
        if props.mute ~= stored_values.mute then
          stored_values.mute = props.mute
          hasChanges = true
        end
        if props.channelVolumes then
          stored_values.channelVolumes = props.channelVolumes
          hasChanges = true
        end
        if props.channelMap then
          stored_values.channelMap = props.channelMap
          hasChanges = true
        end

        ::skip_prop::
      end

      if hasChanges then
        saveStreamProps (key, stored_values)
      end
    end
  end
}

-- save "target.node"/"target.object" on metadata changes
store_stream_target_hook = SimpleEventHook {
  name = "node/store-stream-target-metadata-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "c", "target.object", "target.node" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local nodes_om = source:call ("get-object-manager", "node")
    local props = event:get_properties ()
    local subject_id = props ["event.subject.id"]
    local target_key = props ["event.subject.key"]
    local target_value = props ["event.subject.value"]

    local node = nodes_om:lookup {
      Constraint { "bound-id", "=", subject_id, type = "gobject" }
    }
    if not node then
      return
    end

    local stream_props = node.properties
    cutils.evaluateRulesApplyProperties (stream_props, "stream.rules")

    if stream_props ["state.restore-target"] == "false" then
      return
    end

    local key = formKey (stream_props)
    if not key then
      return
    end

    local target_name = nil

    if target_value and target_value ~= "-1" then
      local target_node
      if target_key == "target.object" then
        target_node = nodes_om:lookup {
          Constraint { "object.serial", "=", target_value, type = "pw-global" }
        }
      else
        target_node = nodes_om:lookup {
          Constraint { "bound-id", "=", target_value, type = "gobject" }
        }
      end
      if target_node then
        target_name = target_node.properties ["node.name"]
      end
    end

    Log.info (node, "saving stream target for " ..
      tostring (stream_props ["node.name"]) .. " -> " .. tostring (target_name))

    local stored_values = getStoredStreamProps (key) or {}
    stored_values.target = target_name
    saveStreamProps (key, stored_values)
  end
}

-- track route-settings metadata changes
route_settings_metadata_added_hook = SimpleEventHook {
  name = "node/route-settings-metadata-added",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-added" },
      Constraint { "metadata.name", "=", "route-settings" },
    },
  },
  execute = function (event)
    local metadata = event:get_subject ()

    -- copy state into the metadata
    local key = "Output/Audio:media.role:Notification"
    local p = getStoredStreamProps (key)
    if p then
      p.channels = p.channelMap and Json.Array (p.channelMap)
      p.volumes = p.channelVolumes and Json.Array (p.channelVolumes)
      p.channelMap = nil
      p.channelVolumes = nil
      p.target = nil
      metadata:set (0, "restore.stream." .. key, "Spa:String:JSON",
          Json.Object (p):to_string ())
    end
  end
}

-- track route-settings metadata changes
route_settings_metadata_changed_hook = SimpleEventHook {
  name = "node/route-settings-metadata-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "route-settings" },
      Constraint { "event.subject.key", "=",
          "restore.stream.Output/Audio:media.role:Notification" },
      Constraint { "event.subject.spa_type", "=", "Spa:String:JSON" },
      Constraint { "event.subject.value", "is-present" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    local subject_id = props ["event.subject.id"]
    local key = props ["event.subject.key"]
    local value = props ["event.subject.value"]

    local json = Json.Raw (value)
    if json == nil or not json:is_object () then
      return
    end

    local vparsed = json:parse ()
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

function buildDefaultChannelVolumes (node)
  local def_vol = config.default_channel_volume
  local channels = 2
  local res = {}

  local str = node.properties["state.default-channel-volume"]
  if str ~= nil then
    def_vol = tonumber (str)
  end

  for pod in node:iterate_params("Format") do
    local pod_parsed = pod:parse()
    if pod_parsed ~= nil then
      channels = pod_parsed.properties.channels
      break
    end
  end

  while (#res < channels) do
    table.insert(res, def_vol)
  end

  return res;
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
  cutils.storeAfterTimeout (state, state_table)
end

function formKey (properties)
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

function toggleState (enable)
  if enable and not state then
    state = State ("stream-properties")
    state_table = state:load ()

    restore_stream_hook:register ()
    store_stream_props_hook:register ()
    store_stream_target_hook:register ()
    route_settings_metadata_changed_hook:register ()

    rs_metadata = ImplMetadata ("route-settings")
    rs_metadata:activate (Features.ALL, function (m, e)
      if e then
        Log.warning ("failed to activate route-settings metadata: " .. tostring (e))
      end
    end)

  elseif not enable and state then
    state = nil
    state_table = nil
    restore_stream_hook:remove ()
    store_stream_props_hook:remove ()
    store_stream_target_hook:remove ()
    route_settings_metadata_changed_hook:remove ()
    rs_metadata = nil
  end
end

config:subscribe ("restore-props", function (enable)
  toggleState (enable or config.restore_target)
end)

config:subscribe ("restore-target", function (enable)
  toggleState (enable or config.restore_props)
end)

toggleState (config.restore_props or config.restore_target)
