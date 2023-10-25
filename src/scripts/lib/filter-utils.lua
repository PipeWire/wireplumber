-- WirePlumber

-- Copyright © 2023 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of filter Lua utility functions

local cutils = require ("common-utils")

local module = {
  metadata = nil,
  filters = {},
}

local function getFilterSmart (metadata, node)
  -- Check metadata
  if metadata ~= nil then
    local id = node["bound-id"]
    local value_str = metadata:find (id, "filter.smart")
    if value_str ~= nil then
      local json = Json.Raw (value_str)
      if json:is_boolean() then
        return json:parse()
      end
    end
  end

  -- Check node properties
  local prop_str = node.properties ["filter.smart"]
  if prop_str ~= nil then
    return cutils.parseBool (prop_str)
  end

  -- Otherwise consider the filter not smart by default
  return false
end

local function getFilterSmartName (metadata, node)
  -- Check metadata
  if metadata ~= nil then
    local id = node["bound-id"]
    local value_str = metadata:find (id, "filter.smart.name")
    if value_str ~= nil then
      local json = Json.Raw (value_str)
      if json:is_string() then
        return json:parse()
      end
    end
  end

  -- Check node properties
  local prop_str = node.properties ["filter.smart.name"]
  if prop_str ~= nil then
    return prop_str
  end

  -- Otherwise use link group as name
  return node.properties ["node.link-group"]
end

local function getFilterSmartDisabled (metadata, node)
  -- Check metadata
  if metadata ~= nil then
    local id = node["bound-id"]
    local value_str = metadata:find (id, "filter.smart.disabled")
    if value_str ~= nil then
      local json = Json.Raw (value_str)
      if json:is_boolean() then
        return json:parse()
      end
    end
  end

  -- Check node properties
  local prop_str = node.properties ["filter.smart.disabled"]
  if prop_str ~= nil then
    return cutils.parseBool (prop_str)
  end

  -- Otherwise consider the filter not disabled by default
  return false
end

local function getFilterSmartTarget (metadata, node, om)
  -- Check metadata and fallback to properties
  local id = node["bound-id"]
  local value_str = nil
  if metadata ~= nil then
    value_str = metadata:find (id, "filter.smart.target")
  end
  if value_str == nil then
    value_str = node.properties ["filter.smart.target"]
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
  for si_target in om:iterate { type = "SiLinkable" } do
    local n_target = si_target:get_associated_proxy ("node")
    if n_target == nil then
      goto skip_target
    end

    -- Target nodes are only meant to be device nodes, without link-group
    if n_target.properties ["node.link-group"] ~= nil then
      goto skip_target
    end

    -- Make sure the target node properties match all rules
    for key, val in pairs(match_rules) do
      if n_target.properties[key] ~= val then
        goto skip_target
      end
    end

    -- Target found
    target = si_target
    break;

    ::skip_target::
  end

  return target;
end

local function getFilterSmartBefore (metadata, node)
  -- Check metadata and fallback to properties
  local id = node["bound-id"]
  local value_str = nil
  if metadata ~= nil then
    value_str = metadata:find (id, "filter.smart.before")
  end
  if value_str == nil then
    value_str = node.properties ["filter.smart.before"]
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

local function getFilterSmartAfter (metadata, node)
  -- Check metadata and fallback to properties
  local id = node["bound-id"]
  local value_str = nil
  if metadata ~= nil then
    value_str = metadata:find (id, "filter.smart.after")
  end
  if value_str == nil then
    value_str = node.properties ["filter.smart.after"]
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

  -- Check if the current filters need to be inserted before or after
  for i, v in ipairs(curr_filters) do
    local insert_before = true
    local insert_after = false

    if v.before ~= nil then
      for j, b in ipairs(v.before) do
        if filter.name == b then
          insert_after = false
          break
        end
      end
    end

    if v.after ~= nil then
      for j, b in ipairs(v.after) do
        if filter.name == b then
          insert_before = false
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

    if insert_before then
      if insert_after then
        Log.warning ("cyclic before/after found in filters " .. v.name .. " and " .. filter.name)
      end
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

  for si in om:iterate { type = "SiLinkable" } do
    local filter = {}

    local n = si:get_associated_proxy ("node")
    if n == nil then
      goto skip_linkable
    end

    -- Only handle nodes with link group (filters)
    filter.link_group = n.properties ["node.link-group"]
    if filter.link_group == nil then
      goto skip_linkable
    end

    -- Only handle the main filter nodes
    filter.media_class = n.properties ["media.class"]
    if filter.media_class ~= "Audio/Sink" and
        filter.media_class ~= "Audio/Source" and
        filter.media_class ~= "Video/Source" then
      goto skip_linkable
    end

    -- Filter direction
    if filter.media_class == "Audio/Sink" then
      filter.direction = "input"
    else
      filter.direction = "output"
    end

    -- Get filter properties
    filter.smart = getFilterSmart (metadata, n)
    filter.name = getFilterSmartName (metadata, n)
    filter.disabled = getFilterSmartDisabled (metadata, n)
    filter.target = getFilterSmartTarget (metadata, n, om)
    filter.before = getFilterSmartBefore (metadata, n)
    filter.after = getFilterSmartAfter (metadata, n)

    -- Add the main and stream session items
    filter.main_si = si
    filter.stream_si = om:lookup {
      type = "SiLinkable",
      Constraint { "node.link-group", "=", filter.link_group },
      Constraint { "media.class", "#", "Stream/*", type = "pw-global" }
    }

    -- Add the filter to the list sorted by before and after
    module.filters = insertFilterSorted (module.filters, filter)

  ::skip_linkable::
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
    local om = source:call ("get-object-manager", "session-item")
    local metadata_om = source:call ("get-object-manager", "metadata")

    rescanFilters (om, metadata_om)
  end
}:register ()

function module.is_filter_smart (direction, link_group)
  -- Make sure direction and link_group is valid
  if direction == nil or link_group == nil then
    return false
  end

  for i, v in ipairs(module.filters) do
    if v.direction == direction and v.link_group == link_group then
      return v.smart
    end
  end

  return false
end

function module.is_filter_disabled (direction, link_group)
  -- Make sure direction and link_group is valid
  if direction == nil or link_group == nil then
    return false
  end

  for i, v in ipairs(module.filters) do
    if v.direction == direction and v.link_group == link_group then
      return v.disabled
    end
  end

  return false
end

function module.get_filter_target (direction, link_group)
  -- Make sure direction and link_group are valid
  if direction == nil or link_group == nil then
    return nil
  end

  -- Find the current filter
  local filter = nil
  local index = nil
  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        v.link_group == link_group and
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
        v.name ~= filter.name and
        v.link_group ~= link_group and
        not v.disabled and
        v.smart and
        ((v.target == nil and v.target == filter.target) or
            (v.target.id == filter.target.id)) and
        i > index then
      return v.main_si
    end
  end

  -- Otherwise return the filter destination target
  return filter.target
end

function module.get_filter_from_target (direction, si_target)
  -- Make sure direction and si_target are valid
  if direction == nil or si_target == nil then
    return nil
  end

  -- Find the first filter matching target
  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        not v.disabled and
        v.smart and
        v.target ~= nil and
        v.target.id == si_target.id then
      return v.main_si
    end
  end

  -- If not found, just return the first filter with nil target
  for i, v in ipairs(module.filters) do
    if v.direction == direction and
        not v.disabled and
        v.smart and
        v.target == nil then
      return v.main_si
    end
  end

  return nil
end

return module
