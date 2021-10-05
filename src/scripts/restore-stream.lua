-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- Based on restore-stream.c from pipewire-media-session
-- Copyright © 2020 Wim Taymans
--
-- SPDX-License-Identifier: MIT

-- the state storage
state = State("restore-stream")
state_table = state:load()

-- simple serializer {"foo", "bar"} -> "foo;bar;"
function serializeArray(a)
  local str = ""
  for _, v in ipairs(a) do
    str = str .. tostring(v) .. ";"
  end
  return str
end

-- simple deserializer "foo;bar;" -> {"foo", "bar"}
function parseArray(str, convert_value)
  local array = {}
  local pos = 1
  while true do
    local next = str:find(";", pos, true)
    if next then
      local val = str:sub(pos, next-1)
      val = convert_value and convert_value(val) or val
      table.insert(array, val)
      pos = next + 1
    else
      break
    end
  end
  return array
end

function parseParam(param, id)
  local route = param:parse()
  if route.pod_type == "Object" and route.object_id == id then
    return route.properties
  else
    return nil
  end
end

function storeAfterTimeout()
  if timeout_source then
    timeout_source:destroy()
  end
  timeout_source = Core.timeout_add(1000, function ()
    local saved, err = state:save(state_table)
    if not saved then
      Log.warning(err)
    end
    timeout_source = nil
  end)
end

function findSuitableKey(node)
  local keys = {
    "media.role",
    "application.id",
    "application.name",
    "media.name",
    "node.name",
  }
  local key = nil
  local node_props = node.properties

  for _, k in ipairs(keys) do
    local p = node_props[k]
    if p then
      key = string.format("%s:%s:%s",
          node_props["media.class"]:gsub("^Stream/", ""), k, p)
      break
    end
  end
  return key
end

function saveTarget(subject, key, type, value)
  if key ~= "target.node" then
    return
  end

  local node = streams_om:lookup {
    Constraint { "bound-id", "=", subject, type = "gobject" }
  }
  if not node then
    return
  end

  local key_base = findSuitableKey(node)
  if not key_base then
    return
  end

  Log.info(node, "saving stream target for " ..
      tostring(node.properties["node.name"]))

  local target_id = value
  local target_name = nil

  if not target_id then
    local metadata = metadata_om:lookup()
    if metadata then
      target_id = metadata:find(node["bound-id"], "target.node")
    end
  end
  if target_id then
    local target_node = allnodes_om:lookup {
      Constraint { "bound-id", "=", target_id, type = "gobject" }
    }
    if target_node then
      target_name = target_node.properties["node.name"]
    end
  end
  state_table[key_base .. ":target"] = target_name

  storeAfterTimeout()
end

function restoreTarget(node, target_name)
  local target_node = allnodes_om:lookup {
    Constraint { "node.name", "=", target_name, type = "pw" }
  }

  if target_node then
    local metadata = metadata_om:lookup()
    if metadata then
      metadata:set(node["bound-id"], "target.node", "Spa:Id",
          target_node["bound-id"])
    end
  end
end

function saveStream(node)
  local key_base = findSuitableKey(node)
  if not key_base then
    return
  end

  Log.info(node, "saving stream props for " ..
      tostring(node.properties["node.name"]))

  for p in node:iterate_params("Props") do
    local props = parseParam(p, "Props")
    if not props then
      goto skip_prop
    end

    if props.volume then
      state_table[key_base .. ":volume"] = tostring(props.volume)
    end
    if props.mute ~= nil then
      state_table[key_base .. ":mute"] = tostring(props.mute)
    end
    if props.channelVolumes then
      state_table[key_base .. ":channelVolumes"] = serializeArray(props.channelVolumes)
    end
    if props.channelMap then
      state_table[key_base .. ":channelMap"] = serializeArray(props.channelMap)
    end

    ::skip_prop::
  end

  storeAfterTimeout()
end

function restoreStream(node)
  local key_base = findSuitableKey(node)
  if not key_base then
    return
  end

  local needsRestore = false
  local props = { "Spa:Pod:Object:Param:Props", "Props" }

  local str = state_table[key_base .. ":volume"]
  needsRestore = str and true or needsRestore
  props.volume = str and tonumber(str) or nil

  local str = state_table[key_base .. ":mute"]
  needsRestore = str and true or needsRestore
  props.mute = str and (str == "true") or nil

  local str = state_table[key_base .. ":channelVolumes"]
  needsRestore = str and true or needsRestore
  props.channelVolumes = str and parseArray(str, tonumber) or nil

  local str = state_table[key_base .. ":channelMap"]
  needsRestore = str and true or needsRestore
  props.channelMap = str and parseArray(str) or nil

  local str = state_table[key_base .. ":target"]
  if str then
    restoreTarget(node, str)
  end

  -- convert arrays to Spa Pod
  if props.channelVolumes then
    table.insert(props.channelVolumes, 1, "Spa:Float")
    props.channelVolumes = Pod.Array(props.channelVolumes)
  end
  if props.channelMap then
    table.insert(props.channelMap, 1, "Spa:Enum:AudioChannel")
    props.channelMap = Pod.Array(props.channelMap)
  end

  if needsRestore then
    Log.info(node, "restore values from " .. key_base)

    local param = Pod.Object(props)
    Log.debug(param, "setting props on " .. tostring(node))
    node:set_param("Props", param)
  end
end

metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
}
metadata_om:connect("object-added", function (om, metadata)
  -- process existing metadata
  for s, k, t, v in metadata:iterate(Id.ANY) do
    saveTarget(s, k, t, v)
  end
  -- and watch for changes
  metadata:connect("changed", function (m, subject, key, type, value)
    saveTarget(subject, key, type, value)
  end)
end)
metadata_om:activate()

allnodes_om = ObjectManager { Interest { type = "node" } }
allnodes_om:activate()

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
streams_om:connect("object-added", function (streams_om, node)
  node:connect("params-changed", saveStream)
  restoreStream(node)
end)
streams_om:connect("object-removed", function (streams_om, node)
  -- clear 'target.node' in case it was set
  -- this needs fixing, it (partly) works only if metadata is WpImplMetadata
  local metadata = metadata_om:lookup()
  if metadata then
    metadata:set(node["bound-id"], "target.node", nil, nil)
  end
end)
streams_om:activate()
