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

function cutils.mediaClassToDirection (media_class)
  if media_class:find ("Sink") or
      media_class:find ("Input") or
      media_class:find ("Duplex") then
    return "input"
  elseif media_class:find ("Source") or media_class:find ("Output") then
    return "output"
  else
    return nil
  end
end

function cutils.getTargetDirection (properties)
  local target_direction = nil

  -- retrun same direction for si-audio-virtual session items
  if properties ["item.factory.name"] == "si-audio-virtual" then
    return properties ["item.node.direction"]
  end

  if properties ["item.node.direction"] == "output" or
      (properties ["item.node.direction"] == "input" and
          cutils.parseBool (properties ["stream.capture.sink"])) then
    target_direction = "input"
  else
    target_direction = "output"
  end
  return target_direction
end

local default_nodes = Plugin.find ("default-nodes-api")

function cutils.getDefaultNode (properties, target_direction)
  local target_media_class =
  properties ["media.type"] ..
      (target_direction == "input" and "/Sink" or "/Source")

  if not default_nodes then
    default_nodes = Plugin.find ("default-nodes-api")
  end

  return default_nodes:call ("get-default-node", target_media_class)
end

cutils.source_plugin = nil
cutils.object_managers = {}

function cutils.get_object_manager (name)
  cutils.source_plugin = cutils.source_plugin or
      Plugin.find ("standard-event-source")
  cutils.object_managers [name] = cutils.object_managers [name] or
      cutils.source_plugin:call ("get-object-manager", name)
  return cutils.object_managers [name]
end

function cutils.get_default_metadata_object ()
  return cutils.get_object_manager ("metadata"):lookup {
    Constraint { "metadata.name", "=", "default" },
  }
end

function cutils.evaluateRulesApplyProperties (properties, name)
  local section = Conf.get_section (name)
  if not section then
    return
  end

  local matched, mprops = JsonUtils.match_rules_update_properties (
      section, properties)

  if (matched > 0 and mprops) then
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

function cutils.arrayContains (a, value)
  for _, v in ipairs (a) do
    if v == value then
      return true
    end
  end
  return false
end

state_save_sources = {}

function cutils.storeAfterTimeout (state, state_table)
  local state_name = state["name"]
  if state_save_sources [state_name] ~= nil then
    state_save_sources [state_name]:destroy ()
    state_save_sources [state_name] = nil
  end
  state_save_sources [state_name] = Core.timeout_add (1000, function ()
    local saved, err = state:save (state_table)
    if not saved then
      Log.warning (err)
    end
    return false
  end)
end

function cutils.get_application_name ()
  return Core.get_properties()["application.name"] or "WirePlumber"
end

function cutils.get_config_section (name, defaults)
  local section = Conf.get_section (name)
  if not section then
    section = defaults or {}
  else
    section = section:parse ()
    if defaults then
      for k, v in pairs (defaults) do
        if section [k] == nil then
          section [k] = v
        end
      end
      for k, v in ipairs (defaults) do
        if section [k] == nil then
          section [k] = v
        end
      end
    end
  end
  return section
end

return cutils
