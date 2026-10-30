-- WirePlumber

-- Copyright © 2023 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of filter Lua utility functions

local cutils = require ("common-utils")
local stream_utils = require ("stream-utils")

local module = {
  metadata = nil,
  filters = {},
  -- filters metadata changed and the graph has not been rescanned yet
  rescan_pending = false,
}

-- Gets a filter metadata value of the stream. The "<key>.<port-group>" key only
-- applies to that stream, and the "<key>" key to all the streams of the node
function module.get_metadata_value (metadata, stream, key)
  local id = stream:get_node_bound_id ()
  local port_group = stream:get_port_group ()
  local value = nil
  if port_group ~= nil then
    value = metadata:find (id, key .. "." .. port_group)
  end
  if value == nil then
    value = metadata:find (id, key)
  end
  return value
end

local function getFilterSmart (metadata, stream)
  -- Check metadata
  if metadata ~= nil then
    local value_str = module.get_metadata_value (metadata, stream, "filter.smart")
    if value_str ~= nil then
      local json = Json.Raw (value_str)
      if json:is_boolean() then
        return json:parse()
      end
    end
  end

  -- Check stream properties
  local prop_str = stream:get_property ("filter.smart")
  if prop_str ~= nil then
    return cutils.parseBool (prop_str)
  end

  -- Otherwise consider the filter not smart by default
  return false
end

local function getFilterSmartName (metadata, stream)
  -- Check metadata
  if metadata ~= nil then
    local value_str = module.get_metadata_value (metadata, stream, "filter.smart.name")
    if value_str ~= nil then
      local json = Json.Raw (value_str)
      if json:is_string() then
        return json:parse()
      end
    end
  end

  -- Check stream properties
  local props = stream:get_properties ()
  local prop_str = props ["filter.smart.name"]
  if prop_str ~= nil then
    return prop_str
  end

  -- Otherwise use link group as name, or the node name if there is none
  return props ["node.link-group"] or props ["node.name"]
end

local function getFilterSmartDisabled (metadata, stream)
  -- Check metadata
  if metadata ~= nil then
    local value_str = module.get_metadata_value (metadata, stream, "filter.smart.disabled")
    if value_str ~= nil then
      local json = Json.Raw (value_str)
      if json:is_boolean() then
        return json:parse()
      end
    end
  end

  -- Check stream properties
  local prop_str = stream:get_property ("filter.smart.disabled")
  if prop_str ~= nil then
    return cutils.parseBool (prop_str)
  end

  -- Otherwise consider the filter not disabled by default
  return false
end

local function getFilterSmartTargetable (metadata, stream)
  -- Check metadata
  if metadata ~= nil then
    local value_str = module.get_metadata_value (metadata, stream, "filter.smart.targetable")
    if value_str ~= nil then
      local json = Json.Raw (value_str)
      if json:is_boolean() then
        return json:parse()
      end
    end
  end

  -- Check stream properties
  local prop_str = stream:get_property ("filter.smart.targetable")
  if prop_str ~= nil then
    return cutils.parseBool (prop_str)
  end

  -- Otherwise consider the filter not targetable by default
  return false
end

local function getFilterSmartTarget (metadata, stream, om)
  -- Check metadata and fallback to properties
  local value_str = nil
  if metadata ~= nil then
    value_str = module.get_metadata_value (metadata, stream, "filter.smart.target")
  end
  if value_str == nil then
    value_str = stream:get_property ("filter.smart.target")
    if value_str == nil then
      return nil
    end
  end

  -- Parse match rules
  local match_rules_json = Json.Raw (value_str)
  if not match_rules_json:is_object () then
    return nil
  end
  local match_rules = match_rules_json:parse ()

  -- Find target
  local target = nil
  for target_stream in om:iterate { type = "Stream" } do
    local target_props = target_stream:get_properties ()

    -- Target streams cannot be smart filters
    if stream_utils.get_filter_peer (target_stream, om) ~= nil and
        getFilterSmart (metadata, target_stream) then
      goto skip_target
    end

    -- Make sure the target stream properties match all rules
    for key, val in pairs(match_rules) do
      if target_props[key] ~= tostring (val) then
        goto skip_target
      end
    end

    -- Target found
    target = target_stream
    break;

    ::skip_target::
  end

  return target
end

local function getFilterSmartTargetless (metadata, stream)
  local value_str = nil
  if metadata ~= nil then
    value_str = module.get_metadata_value (metadata, stream, "filter.smart.target")
  end
  if value_str == nil then
    value_str = stream:get_property ("filter.smart.target")
  end

  return value_str == nil
end

local function getFilterSmartBefore (metadata, stream)
  -- Check metadata and fallback to properties
  local value_str = nil
  if metadata ~= nil then
    value_str = module.get_metadata_value (metadata, stream, "filter.smart.before")
  end
  if value_str == nil then
    value_str = stream:get_property ("filter.smart.before")
    if value_str == nil then
      return nil
    end
  end

  -- Parse
  local before_json = Json.Raw (value_str)
  if not before_json:is_array() then
    return nil
  end
  return before_json:parse ()
end

local function getFilterSmartAfter (metadata, stream)
  -- Check metadata and fallback to properties
  local value_str = nil
  if metadata ~= nil then
    value_str = module.get_metadata_value (metadata, stream, "filter.smart.after")
  end
  if value_str == nil then
    value_str = stream:get_property ("filter.smart.after")
    if value_str == nil then
      return nil
    end
  end

  -- Parse
  local after_json = Json.Raw (value_str)
  if not after_json:is_array() then
    return nil
  end
  return after_json:parse ()
end

local function insertFilterSorted (curr_filters, filter)
  local before_filters = {}
  local after_filters = {}
  local new_filters = {}
  local found_after = false

  -- Check if the current filters need to be inserted before or after
  for i, v in ipairs(curr_filters) do
    local insert_before = false
    local insert_after = false

    if v.before ~= nil then
      for j, b in ipairs(v.before) do
        if filter.name == b then
          insert_before = true
          break
        end
      end
    end

    if v.after ~= nil then
      for j, b in ipairs(v.after) do
        if filter.name == b then
          insert_after = true
          break
        end
      end
    end

    if filter.before ~= nil then
      for j, b in ipairs(filter.before) do
        if v.name == b then
          insert_after = true
        end
      end
    end

    if filter.after ~= nil then
      for j, b in ipairs(filter.after) do
        if v.name == b then
          insert_before = true
        end
      end
    end

    if insert_before and insert_after then
      Log.warning ("cyclic before/after found in filters " .. v.name .. " and " .. filter.name)
    end

    -- Filters following one that goes after keep their order, unless they
    -- have to go before
    found_after = found_after or insert_after
    if insert_before or not found_after then
      table.insert (before_filters, v)
    else
      table.insert (after_filters, v)
    end

  end

  -- Add the filters to the new table stored
  for i, v in ipairs(before_filters) do
    table.insert (new_filters, v)
  end
  table.insert (new_filters, filter)
  for i, v in ipairs(after_filters) do
    table.insert (new_filters, v)
  end

  return new_filters
end

local function rescanFilters (om, metadata_om)
  local metadata =
        metadata_om:lookup { Constraint { "metadata.name", "=", "filters" } }

  -- Always clear all filters data on rescan
  module.filters = {}

  Log.info ("rescanning filters...")

  for stream in om:iterate { type = "Stream" } do
    local filter = {}

    -- Only handle filter streams, which are the ones with a peer
    filter.stream = stream_utils.get_filter_peer (stream, om)
    if filter.stream == nil then
      goto skip_stream
    end

    -- Only handle the main filter streams, which are the ones shaped as a
    -- sink or a source
    local shape = stream:get_shape ()
    if shape ~= StreamShape.SINK and shape ~= StreamShape.SOURCE then
      goto skip_stream
    end

    -- Filter direction and media type
    filter.direction = stream:get_direction ()
    filter.media_type = stream_utils.get_media_type (stream)

    -- Get filter properties
    filter.smart = getFilterSmart (metadata, stream)
    filter.name = getFilterSmartName (metadata, stream)
    filter.disabled = getFilterSmartDisabled (metadata, stream)
    filter.targetable = getFilterSmartTargetable (metadata, stream)
    filter.target = getFilterSmartTarget (metadata, stream, om)
    filter.targetless = getFilterSmartTargetless (metadata, stream)
    filter.before = getFilterSmartBefore (metadata, stream)
    filter.after = getFilterSmartAfter (metadata, stream)

    -- Add the main stream
    filter.main_stream = stream

    -- Add the filter to the list sorted by before and after
    module.filters = insertFilterSorted (module.filters, filter)

  ::skip_stream::
  end

end

SimpleEventHook {
  name = "lib/filter-utils/rescan",
  before = "linking/rescan",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "rescan-for-linking" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local om = source:call ("get-object-manager", "stream")
    local metadata_om = source:call ("get-object-manager", "metadata")

    rescanFilters (om, metadata_om)
    module.rescan_pending = false
  end
}:register ()

SimpleEventHook {
  name = "lib/filter-utils/metadata-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "filters" },
    },
  },
  execute = function (event)
    -- Update the filters data right away, so that streams added before the
    -- next rescan are linked using the new filters configuration
    local source = event:get_source ()
    local om = source:call ("get-object-manager", "stream")
    local metadata_om = source:call ("get-object-manager", "metadata")

    rescanFilters (om, metadata_om)
    module.rescan_pending = true
  end
}:register ()

function module.is_rescan_pending ()
  return module.rescan_pending
end

function module.is_filter_smart (direction, stream)
  -- Make sure direction is valid
  if direction == nil then
    return false
  end

  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        stream_utils.is_same_filter (v.main_stream, stream) then
      return v.smart
    end
  end

  return false
end

function module.is_filter_disabled (direction, stream)
  -- Make sure direction is valid
  if direction == nil then
    return false
  end

  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        stream_utils.is_same_filter (v.main_stream, stream) then
      return v.disabled
    end
  end

  return false
end

function module.is_filter_targetable (direction, stream)
  -- Make sure direction is valid
  if direction == nil then
    return false
  end

  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        stream_utils.is_same_filter (v.main_stream, stream) then
      return v.targetable
    end
  end

  return false
end

function module.get_filter_target (direction, stream)
  -- Make sure direction is valid
  if direction == nil then
    return nil
  end

  -- Find the current filter
  local filter = nil
  local index = nil
  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        stream_utils.is_same_filter (v.main_stream, stream) and
        not v.disabled and
        v.smart then
      filter = v
      index = i
      break
    end
  end
  if filter == nil then
    return nil
  end

  -- Return the next filter with matching target
  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        v.media_type == filter.media_type and
        v.name ~= filter.name and
        not stream_utils.is_same_filter (v.main_stream, stream) and
        not v.disabled and
        v.smart and
        ((v.target == nil and filter.target == nil) or
            (v.target ~= nil and filter.target ~= nil and v.target.id == filter.target.id)) and
        i > index then
      return v.main_stream
    end
  end

  -- Otherwise return the filter destination target
  return filter.target
end

function module.get_filter_from_target (direction, media_type, target_stream, stream_om)
  local target = target_stream

  -- Make sure direction and media_type are valid
  if direction == nil or media_type == nil then
    return nil
  end

  -- If target_stream is a filter, find it and use its target
  if target_stream then
    if stream_utils.get_filter_peer (target_stream, stream_om) ~= nil then
      local filter = nil
      for i, v in ipairs(module.filters) do
        if v.direction == direction and
            v.media_type == media_type and
            stream_utils.is_same_filter (v.main_stream, target_stream) and
            not v.disabled and
            v.smart then
          filter = v
          break
        end
      end
      if filter == nil then
        return nil
      end
      target = filter.target
    end
  end

  -- Find the first filter matching target
  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        v.media_type == media_type and
        not v.disabled and
        v.smart and
        ((v.target ~= nil and target ~= nil and v.target.id == target.id) or
            (target == nil and v.targetless)) then
      return v.main_stream
    end
  end

  return nil
end

return module
