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
      key = string.format("%s:%s:%s:",
          node_props["media.class"]:gsub("^Stream/", ""), k, p)
    end
  end
  return key
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
      state_table[key_base .. "volume"] = tostring(props.volume)
    end
    if props.mute ~= nil then
      state_table[key_base .. "mute"] = tostring(props.mute)
    end
    if props.channelVolumes then
      state_table[key_base .. "channelVolumes"] = serializeArray(props.channelVolumes)
    end
    if props.channelMap then
      state_table[key_base .. "channelMap"] = serializeArray(props.channelMap)
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

  local str = state_table[key_base .. "volume"]
  needsRestore = str and true or needsRestore
  props.volume = str and tonumber(str) or nil

  local str = state_table[key_base .. "mute"]
  needsRestore = str and true or needsRestore
  props.mute = str and (str == "true") or nil

  local str = state_table[key_base .. "channelVolumes"]
  needsRestore = str and true or needsRestore
  props.channelVolumes = str and parseArray(str, tonumber) or nil

  local str = state_table[key_base .. "channelMap"]
  needsRestore = str and true or needsRestore
  props.channelMap = str and parseArray(str) or nil

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

om = ObjectManager {
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

om:connect("object-added", function (om, node)
  node:connect("params-changed", saveStream)
  restoreStream(node)
end)

om:activate()
