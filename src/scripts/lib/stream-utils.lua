-- WirePlumber

-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of stream Lua utility functions

local cutils = require ("common-utils")
local nutils = require ("node-utils")

local module = {}

function module.get_media_type (stream)
  local audio_interest = Interest { type = "AudioStream" }
  local video_interest = Interest { type = "VideoStream" }
  if audio_interest:matches (stream) then
    return "Audio"
  elseif video_interest:matches (stream) then
    return "Video"
  end
  return "Unknown"
end

-- Gets a "stream.<name>" property of the stream, set in the node's
-- "stream.props.<port-group>" property, or the "node.<name>" property, which
-- applies to all the streams of the node, if the stream one is not set
function module.get_boolean_property (stream_props, name)
  local value = stream_props:get_boolean ("stream." .. name)
  if value == nil then
    value = stream_props:get_boolean ("node." .. name)
  end
  return value
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

-- Checks if both streams have the same media type and belong to the same node
-- or the same link group
function module.is_same_filter (stream, other)
  if module.get_media_type (stream) ~= module.get_media_type (other) then
    return false
  end
  if stream:get_node_bound_id () == other:get_node_bound_id () then
    return true
  end
  local link_group = stream:get_property ("node.link-group")
  return link_group ~= nil and
      other:get_property ("node.link-group") == link_group
end

-- Gets the stream on the other side of a filter: the first stream of the same
-- media type and matching shape from the same node or the same link group
function module.get_filter_peer (stream, stream_om)
  local peer_shapes = {
    [StreamShape.INPUT] = StreamShape.SOURCE,
    [StreamShape.OUTPUT] = StreamShape.SINK,
    [StreamShape.SINK] = StreamShape.OUTPUT,
    [StreamShape.SOURCE] = StreamShape.INPUT,
  }
  local peer_shape = peer_shapes [stream:get_shape ()]
  local media_type = module.get_media_type (stream)
  if peer_shape == nil or media_type == "Unknown" then
    return nil
  end

  for peer in stream_om:iterate {
      type = media_type .. "Stream",
      Constraint { "shape", "=", peer_shape, type = "gobject" },
    } do
    if module.is_same_filter (stream, peer) then
      return peer
    end
  end
  return nil
end

-- Checks if the stream belongs to a filter. A stream with a link group always
-- does, even if its filter peer was not added yet
function module.is_filter_stream (stream, stream_om)
  return stream:get_property ("node.link-group") ~= nil or
      module.get_filter_peer (stream, stream_om) ~= nil
end

function module.can_link (stream, target, stream_om)
  local stream_dir = stream:get_direction ()
  local target_dir = target:get_direction ()
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

  -- Function to check the filter
  local visited = {}
  local function can_link_filter_check (target, hops)
    -- Peers link back to each other, so only check each stream once
    if visited [target.id] then
      return true
    end
    visited [target.id] = true

    local target_id = target.id
    local target_dir = target:get_direction ()

    -- Sanity check to avoid infinite recursion
    if hops == 8 then
      return false
    end

    -- Allow linking if target is not a filter stream
    if not module.is_filter_stream (target, stream_om) then
      return true
    end

    -- Do not allow linking if target belongs to the same filter
    if module.is_same_filter (stream, target) then
      return false
    end

    -- Make sure target is not linked with another stream of the same filter
    for target_peer in StreamLinkManager.iterate_peers (target) do
      -- The stream itself is a peer if it is already linked to the target
      if target_peer ~= stream and
          not can_link_filter_check (target_peer, hops + 1) then
        return false
      end
    end

    return true
  end

  if not module.is_filter_stream (stream, stream_om) then
    return true
  end

  -- The stream of a role loopback must not link to another role-based target
  if target_props:get_boolean ("policy.role-based.target") then
    for role_stream in stream_om:iterate {
        type = "Stream",
        Constraint { "policy.role-based.target", "=", true, type = "wp" },
      } do
      if module.is_same_filter (stream, role_stream) then
        return false
      end
    end
  end

  -- Avoid circular links in the graph
  return can_link_filter_check (target, 0)
end

function module.check_follow_default (stream, target, stream_om)
  local stream_props = stream:get_properties ()
  local reconnect =
      not module.get_boolean_property (stream_props, "dont-reconnect")

  if reconnect and not module.is_filter_stream (stream, stream_om) then
    local default_streams = Plugin.find ("default-streams-api")
    if default_streams ~= nil then
      local target_media_class = module.get_target_media_class (stream)
      if target_media_class ~= nil then
        local default_stream = default_streams:call (
            "get-default-stream", target_media_class)
        if default_stream ~= nil and target:get_node_bound_id () ==
            default_stream:get_node_bound_id () then
          local metadata = cutils.get_default_metadata_object ()
          local node_id = stream:get_node_bound_id ()
          local port_group = stream:get_port_group ()
          -- The node key would also apply to the other streams of the node
          if port_group then
            local value = Json.Object { ["target.object"] = "-1" }
            metadata:set (node_id, "target.stream." .. port_group,
                "Spa:String:JSON", value:to_string ())
          else
            metadata:set (node_id, "target.node", "Spa:Id", "-1")
          end
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

-- A target is a role-based policy target only while handle-media-role-links is
-- loaded: that script is what activates the links to such targets.
function module.is_role_policy_target (stream, target)
  local stream_props = stream:get_properties ()
  local target_props = target:get_properties ()

  return Core.test_feature ("hooks.linking.stream-link.handle-media-role") and
      target_props:get_boolean ("policy.role-based.target") and
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

-- Streams that have been linked at least once, by stream id. A stream that
-- lost its link is not the same as one that was never linked: the first is
-- waiting for its target to come back. Cleared in create-stream-link.lua.
module.linked_once = {}

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
