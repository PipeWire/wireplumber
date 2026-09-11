-- WirePlumber

-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of stream Lua utility functions

local cutils = require ("common-utils")
local nutils = require ("node-utils")

local module = {}

function module.get_media_type (stream)
  local stream_props = stream:get_properties ()
  local audio_interest = Interest { type = "AudioStream" }
  local video_interest = Interest { type = "VideoStream" }
  if audio_interest:matches (stream) then
    return "Audio"
  elseif video_interest:matches (stream) then
    return "Video"
  end
  return "Unknown"
end

function module.get_target_direction (stream)
  local stream_dir = stream:get_direction ()
  local stream_props = stream:get_properties ()
  if stream_dir == Direction.OUTPUT or
      (stream_dir == Direction.INPUT and
          stream_props:get_boolean ("stream.capture.sink")) then
    return Direction.INPUT
  end
  return Direction.OUTPUT
end

function module.get_target_shape (stream)
  local stream_dir = stream:get_direction ()
  local stream_props = stream:get_properties ()
  if stream_dir == Direction.OUTPUT or
      (stream_dir == Direction.INPUT and
          stream_props:get_boolean ("stream.capture.sink")) then
    return StreamShape.SINK
  end
  return StreamShape.SOURCE
end

function module.get_target_media_class (stream)
  local media_type = module.get_media_type (stream)
  if media_type == "Unknown" then
    return nil
  end
  if module.get_target_shape (stream) == StreamShape.SINK then
    return media_type .. "/Sink"
  end
  return media_type .. "/Source"
end

function module.can_link (stream, target)
  local stream_dir = stream:get_direction ()
  local target_dir = target:get_direction ()
  local stream_props = stream:get_properties ()
  local target_props = target:get_properties ()

  -- Streams must have opposite direction, otherwise they must be both input
  -- and the target must have valid monitor ports
  if stream_dir == target_dir then
    if stream_dir == Direction.OUTPUT then
      return false
    end
    local audio_interest = Interest { type = "AudioStream" }
    if not audio_interest:matches (stream) or
        not audio_interest:matches (target) or
        not target:is_monitorable (stream) then
      return false
    end
  end

  -- Function to check the link group
  local function can_link_group_check (stream_link_group, target, hops)
    local target_id = target.id
    local target_dir = target:get_direction ()
    local target_props = target:get_properties ()

    -- Sanity check to avoid infinite recursion
    if hops == 8 then
      return false
    end

    -- Allow linking if target has no link-group property
    local target_link_group = target_props ["node.link-group"]
    if target_link_group == nil then
      return true
    end

    -- Do not allow linking if target has the same link group
    if stream_link_group == target_link_group then
      return false
    end

    -- Make sure target is not linked with another stream with same link group
    for target_peer in StreamLinkManager.iterate_peers (target) do
      if not can_link_group_check (stream_link_group, target_peer, hops + 1) then
        return false
      end
    end

    return true
  end

  local stream_link_group = stream_props ["node.link-group"]

  -- The stream of a role loopback must not link to another role-based target
  if stream_link_group ~= nil and
      target_props:get_boolean ("policy.role-based.target") and
      cutils.get_object_manager ("stream"):lookup {
        type = "Stream",
        Constraint { "node.link-group", "=", stream_link_group },
        Constraint { "policy.role-based.target", "=", true },
      } then
    return false
  end

  -- Avoid circular links in the graph if stream belongs to a link group
  if stream_link_group ~= nil then
    return can_link_group_check (stream_link_group, target, 0)
  end

  return true
end

function module.check_follow_default (stream, target)
  local stream_props = stream:get_properties ()
  local target_props = target:get_properties ()
  local reconnect = not stream_props:get_boolean ("node.dont-reconnect")

  if reconnect and stream_props ["node.link-group"] == nil then
    local default_nodes = Plugin.find ("default-nodes-api")
    if default_nodes ~= nil then
      local target_media_class = module.get_target_media_class (stream)
      if target_media_class ~= nil then
        local default_node_id = default_nodes:call (
            "get-default-node", target_media_class)
        if target_props ["node.id"] == tostring (default_node_id) then
          local metadata = cutils.get_default_metadata_object ()
          metadata:set (stream:get_node_bound_id (), "target.node", "Spa:Id", "-1")
          Log.info (stream, "... set metadata to follow default")
        end
      end
    end
  end
end

function module.has_available_routes (stream, devices_om)
  local stream_node = stream:get_node ()
  return nutils.has_available_routes (stream_node, devices_om)
end

function module.is_role_policy_target (stream, target)
  local stream_props = stream:get_properties ()
  local target_props = target:get_properties ()

  return target_props:get_boolean ("policy.role-based.target") and
      not stream_props:get_boolean ("stream.monitor") and
      stream:get_direction () ~= target:get_direction ()
end

-- A role-based stream link connects a media-role stream (a client stream
-- with a "media.role") to a device-role stream (a target marked with
-- "policy.role-based.target", e.g. a role-based loopback sink). Returns
-- both streams, or nil if the link is not role-based.
function module.get_role_streams (stream_link)
  local out_stream = stream_link:get_output_stream ()
  local in_stream = stream_link:get_input_stream ()
  if out_stream == nil or in_stream == nil then
    return nil, nil
  end

  if module.is_role_policy_target (in_stream, out_stream) then
    return out_stream, in_stream
  elseif module.is_role_policy_target (out_stream, in_stream) then
    return in_stream, out_stream
  end

  return nil, nil
end

-- Returns whether the given stream link is role-based, along with its
-- device-role and media-role streams, or false if it is not (e.g. its
-- media-role stream is a monitor stream)
function module.is_role_based_link (stream_link)
  local device_stream, media_role_stream = module.get_role_streams (stream_link)
  return device_stream ~= nil, device_stream, media_role_stream
end

function module.send_client_error (stream, source, code, message)
  local node_id = stream:get_node_bound_id ()
  local stream_props = stream:get_properties ()
  local client_id = stream_props:get_int ("client.id")
  if client_id ~= nil then
    local clients_om = source:call ("get-object-manager", "client")
    local client = clients_om:lookup {
        Constraint { "bound-id", "=", client_id, type = "gobject" }
    }
    if client ~= nil then
      client:send_error (node_id, code, message)
      return true
    end
  end
  return false
end

return module
