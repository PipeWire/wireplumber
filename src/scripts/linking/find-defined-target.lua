-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target stream is defined explicitly.
-- This definition can be done in two ways.
-- 1. "node.target"/"target.object" in the node properties
-- 2. "target.node"/"target.object" in the default metadata

stream_utils = require ("stream-utils")

log = Log.open_topic ("s-linking")

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

    -- Get the target key and value
    if stream_props ["target.object"] ~= nil then
      target_value = stream_props ["target.object"]
      target_key = "object.serial"
    elseif stream_props ["node.target"] ~= nil then
      target_value = stream_props ["node.target"]
      target_key = "object.id"
    end

    -- Check metadata if allow moving streams setting is enabled
    if Settings.get_boolean ("linking.allow-moving-streams") then
      metadata = metadata_om:lookup {
          Constraint { "metadata.name", "=", "default" }
      }
    end
    if metadata ~= nil and not dont_move then
      local id = metadata:find (node_id, "target.object")
      if id ~= nil then
        target_value = id
        target_key = "object.serial"
      else
        id = metadata:find (node_id, "target.node")
        if id ~= nil then
          target_value = id
          target_key = "object.id"
        end
      end
    end

    -- Don't do anything if target key and value are not valid
    if target_key == nil or target_value == nil or target_value == "-1" then
      log:debug (stream, "target key and value not defined")
      target = nil
      goto done
    end

    -- If target-value is a number, find the target using target-key,
    -- otherwise just find the target by checking all identity properties.
    if tonumber (target_value) then
      if target_key == "object.id" then
        for candidate_stream in stream_om:iterate { type = "Stream" } do
          if tostring (candidate_stream:get_node_bound_id ()) == target_value then
            target_stream = candidate_stream
            break
          end
        end
      else
        target_stream = stream_om:lookup {
          type = "Stream",
          Constraint { target_key, "=", target_value },
        }
      end
      if target_stream ~= nil then
        if stream_utils.can_link (stream, target_stream) then
          target = target_stream
        else
          log:debug (stream, "found defined target cannot be linked")
        end
      end
    else
      local target_dir = stream_utils.get_target_direction (stream)
      for target_stream in stream_om:iterate {
          type = "Stream",
          Constraint { "direction", "=", target_dir, type = "gobject" },
      } do
        local target_props = target_stream:get_properties()
        if (target_props ["node.name"] == target_value or
            target_props ["object.path"] == target_value) then
          if stream_utils.can_link (stream, target_stream) then
            target = target_stream
          else
            log:debug (stream, "found defined target cannot be linked")
          end
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
    elseif dont_fallback then
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
