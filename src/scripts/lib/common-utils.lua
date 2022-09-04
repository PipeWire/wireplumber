-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of common Lua utility functions

local cutils = {}

function cutils.parseBool (var)
  return var and (var:lower () == "true" or var == "1")
end

function cutils.parseParam (param, id)
  local props = param:parse ()
  if props.pod_type == "Object" and props.object_id == id then
    return props.properties
  else
    return nil
  end
end

function cutils.getTargetDirection (properties)
  local target_direction = nil
  if properties ["item.node.direction"] == "output" or
      (properties ["item.node.direction"] == "input" and
          cutils.parseBool (properties ["stream.capture.sink"])) then
    target_direction = "input"
  else
    target_direction = "output"
  end
  return target_direction
end

function cutils.getDefaultNode (properties, target_direction)
  local target_media_class =
  properties ["media.type"] ..
      (target_direction == "input" and "/Sink" or "/Source")
  return default_nodes:call ("get-default-node", target_media_class)
end

default_nodes = Plugin.find ("default-nodes-api")

cutils.default_metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
}

function cutils.evaluateRulesApplyProperties (properties, name)
  local matched, mprops = Settings.apply_rule (name, properties)

  if (matched and mprops) then
    for k, v in pairs (mprops) do
      properties [k] = v
    end
  end
end

-- simple serializer {"foo", "bar"} -> "foo;bar;"
function cutils.serializeArray (a)
  local str = ""
  for _, v in ipairs (a) do
    str = str .. tostring (v):gsub (";", "\\;") .. ";"
  end
  return str
end

-- simple deserializer "foo;bar;" -> {"foo", "bar"}
function cutils.parseArray (str, convert_value, with_type)
  local array = {}
  local val = ""
  local escaped = false
  for i = 1, #str do
    local c = str:sub (i, i)
    if c == '\\' then
      escaped = true
    elseif c == ';' and not escaped then
      val = convert_value and convert_value (val) or val
      table.insert (array, val)
      val = ""
    else
      val = val .. tostring (c)
      escaped = false
    end
  end
  if with_type then
    array ["pod_type"] = "Array"
  end
  return array
end

cutils.default_metadata_om:activate ()

return cutils