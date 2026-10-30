-- WirePlumber

-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of stream Lua utility functions

local cutils = require ("common-utils")

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

function module.get_session_priority (stream)
  local stream_props = stream:get_properties ()
  local priority = stream_props ["priority.session"]
  -- fallback to driver priority if session priority is not set
  if not priority then
    priority = stream_props ["priority.driver"]
  end
  return math.tointeger (priority) or 0
end

-- object.serial is assigned by PipeWire from a single monotonic counter, so
-- serials of any two objects are comparable and the lower one is the older
-- object. Objects that do not report one sort last and cannot be told apart.
local function get_object_serial (props)
  local serial = props and props ["object.serial"]
  return (serial and math.tointeger (tonumber (serial))) or math.maxinteger
end

-- Builds the key that module.compare_stream_rankings() ranks a stream by:
--
--  priority       the session priority, or the driver priority if unset
--  group_serial   the object.serial of the device the stream belongs to, or
--                 its own if it has none; streams of the same device share it
--  route_priority the priority of the device route that carries this stream
--  serial         the stream's own object.serial
function module.get_stream_ranking (stream, devices_om)
  local stream_props = stream:get_properties ()
  local serial = get_object_serial (stream_props)
  local ranking = {
    priority = module.get_session_priority (stream),
    group_serial = serial,
    route_priority = 0,
    serial = serial,
  }

  local card_profile_device = stream_props ["card.profile.device"]
  local device_id = stream_props ["device.id"]

  -- if the stream does not have an associated device, it is its own group
  if not card_profile_device or not device_id then
    return ranking
  end

  -- Get the device
  local device = devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }

  if not device then
    return ranking
  end

  ranking.group_serial = get_object_serial (device.properties)

  -- Get the priority of the associated route
  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if route and (route.device == tonumber (card_profile_device)) then
      ranking.route_priority = route.priority
      break
    end
  end

  return ranking
end

-- Returns true if stream ranking 'a' should be preferred over ranking 'b'.
--
-- The device serial outranks the route priority because route priorities rank
-- the outputs of one card against each other and nothing more -- ACP prices
-- analog-output at 9900 and hdmi-output-0 at 5900 on every card that has them
-- -- so only streams of the same device may be compared on it.
--
-- The stream serial makes the outcome independent of the iteration order, which
-- a WpObjectManager reshuffles on every removal. Both serials rank the older
-- object first; between two cards that is the order the PipeWire monitor
-- discovered them in, which is arbitrary, but a tie on priority.session means
-- no preference was expressed.
function module.compare_stream_rankings (a, b)
  if a.priority ~= b.priority then
    return a.priority > b.priority
  elseif a.group_serial ~= b.group_serial then
    return a.group_serial < b.group_serial
  elseif a.route_priority ~= b.route_priority then
    return a.route_priority > b.route_priority
  else
    return a.serial < b.serial
  end
end

function module.has_available_routes (stream, devices_om)
  local stream_props = stream:get_properties ()
  local device_id = stream_props:get_int ("device.id")
  local card_profile_device = stream_props ["card.profile.device"]

  -- Consider routes as available if the stream is not associated with any device
  if device_id == nil or card_profile_device == nil then
    return true
  end
  local device = devices_om:lookup {
    Constraint { "bound-id", "=", device_id, type = "gobject" },
  }
  if device == nil then
    return true
  end

  -- Check if at least one of the current device routes supports the stream's
  -- card profile device and is available
  for p in device:iterate_params ("Route") do
    local route = cutils.parseParam (p, "Route")
    if route and (route.device == tonumber (card_profile_device)) then
      if (route.available == "no") then
        return false
      else
        return true
      end
    end
  end

  -- Check if available routes support the stream's card profile device
  local found = 0
  for r in device:iterate_params ("EnumRoute") do
    local route = cutils.parseParam (r, "EnumRoute")
    if route and type (route.devices) == "table" then
      for _, i in ipairs (route.devices) do
        if i == tonumber (card_profile_device) then
          found = found + 1
          if route.available ~= "no" then
            return true
          end
        end
      end
    end
  end

  -- The stream is part of a profile without routes so we assume it is available
  if found == 0 then
    return true
  end

  return false
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
