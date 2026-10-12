-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target stream is defined explicitly.
-- This definition can be done in four ways, from lowest to highest priority:
-- 1. "node.target"/"target.object" in the node properties
-- 2. "stream.props.target.stream" in the stream properties
-- 3. "target.node"/"target.object" in the default metadata
-- 4. "target.stream.<port-group>" in the default metadata
--
-- The "target.stream" values are JSON objects with a "target.object" key,
-- which selects the target node, and an optional "target.stream" key, which
-- selects the port group of the target stream in that node.

stream_utils = require ("stream-utils")

log = Log.open_topic ("s-linking")

-- Returns the target object and the optional target port group of a
-- "target.stream" JSON value, or nil if the target object is missing
local function parse_target_stream (value)
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

SimpleEventHook {
  name = "linking/find-defined-target",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream = event:get_subject ()
    local stream_props = stream:get_properties ()
    local stream_id = stream.id
    local node_id = stream:get_node_bound_id ()
    local dont_fallback = stream_props:get_boolean ("node.dont-fallback")
    local dont_move = stream_props:get_boolean ("node.dont-move")

    -- Bypass the hook if the target is already picked up
    local target = event:get_data ("target")
    if target ~= nil then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    local stream_om = source:call ("get-object-manager", "stream")
    local metadata_om = source:call ("get-object-manager", "metadata")
    local metadata = nil
    local target_key = nil
    local target_value = nil
    local target_port_group = nil
    local node_defined = false

    -- can_link() only checks the direction, the monitor ports and the link
    -- groups, so the formats need is_compatible() as well
    local function target_is_usable (candidate)
      if not stream_utils.can_link (stream, candidate) then
        log:debug (stream, "found defined target cannot be linked")
        return false
      end
      if not stream:is_compatible (candidate) then
        log:debug (stream, "found defined target is not compatible")
        return false
      end
      return true
    end

    -- Get the target key and value
    local object, group = parse_target_stream (
        stream_props ["stream.props.target.stream"])
    if object ~= nil then
      target_value = object
      target_port_group = group
      target_key = "object.serial"
    elseif stream_props ["target.object"] ~= nil then
      target_value = stream_props ["target.object"]
      target_key = "object.serial"
      node_defined = true
    elseif stream_props ["node.target"] ~= nil then
      target_value = stream_props ["node.target"]
      target_key = "object.id"
      node_defined = true
    end

    -- Check metadata if allow moving streams setting is enabled
    if Settings.get_boolean ("linking.allow-moving-streams") then
      metadata = metadata_om:lookup {
          Constraint { "metadata.name", "=", "default" }
      }
    end
    if metadata ~= nil and not dont_move then
      local port_group = stream:get_port_group ()
      local object, group = nil, nil
      if port_group ~= nil then
        object, group = parse_target_stream (
            metadata:find (node_id, "target.stream." .. port_group))
      end
      if object ~= nil then
        target_value = object
        target_port_group = group
        target_key = "object.serial"
        node_defined = false
      else
        local id = metadata:find (node_id, "target.object")
        if id ~= nil then
          target_value = id
          target_port_group = nil
          target_key = "object.serial"
          node_defined = false
        else
          id = metadata:find (node_id, "target.node")
          if id ~= nil then
            target_value = id
            target_port_group = nil
            target_key = "object.id"
            node_defined = false
          end
        end
      end
    end

    -- Don't do anything if target key and value are not valid
    if target_key == nil or target_value == nil or target_value == "-1" then
      log:debug (stream, "target key and value not defined")
      goto done
    end

    -- If target-value is a number, find the target using target-key,
    -- otherwise just find the target by checking all identity properties.
    -- If a target port group is defined, the target must also have it.
    -- The direction is not checked here, as a capture stream can also be
    -- linked to the monitor ports of a sink, so the first usable stream wins.
    do
      for candidate in stream_om:iterate { type = "Stream" } do
        local candidate_props = candidate:get_properties ()
        local matches
        if not tonumber (target_value) then
          matches = candidate_props ["node.name"] == target_value or
              candidate_props ["object.path"] == target_value
        elseif target_key == "object.id" then
          matches = tostring (candidate:get_node_bound_id ()) == target_value
        else
          matches = candidate_props [target_key] == target_value
        end
        if matches and (target_port_group == nil or
            candidate:get_port_group () == target_port_group) and
            target_is_usable (candidate) then
          target = candidate
          break
        end
      end
    end

    ::done::
    if target ~= nil then
      -- set event target and continue
      local target_props = target:get_properties()
      local target_id = target.id
      local target_node_id = target:get_node_bound_id ()
      log:info (stream, string.format (
          "found defined target stream:%d (node:%d): %s",
          target_id, target_node_id, tostring (target_props["node.name"])))
      event:set_data ("target", target)
      event:set_data ("is-defined-target", true)
      event:set_data ("is-node-defined-target", node_defined)
    elseif dont_fallback and target_value ~= nil then
      -- send error to client and destroy node if linger is not set
      local linger = stream_props:get_boolean ("node.linger")
      if not linger then
        local message = "defined target not found"
        if stream_utils.send_client_error (stream, source, -2, message) then
          log:debug (stream, "sent client error with message: " .. message)
        end
        local node = stream:get_node ()
        node:request_destroy ()
        log:debug (stream, "requested destroy node as defined target was not found")
      else
        log:debug (stream, "waiting for defined target as dont-fallback is set")
      end

      -- stop processing further hooks
      event:stop_processing ()
    else
      -- continue with other hooks
      log:info (stream, "could not find defined target stream")
    end
  end
}:register ()
