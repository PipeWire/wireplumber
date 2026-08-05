-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Links a stream to the target that has been previously selected.
-- This is meant to be the last hook in the select-target chain.

stream_utils = require ("stream-utils")

log = Log.open_topic ("s-linking")

AsyncEventHook {
  name = "linking/create-stream-link",
  after = "linking/prepare-stream-link",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local stream = event:get_subject ()
        local stream_props = stream:get_properties ()
        local stream_id = stream.id
        local node_id = stream:get_node_bound_id ()
        local out_stream = nil
        local in_stream = nil

        -- Bypass the hook if the target was not found
        local target = event:get_data ("target")
        if target == nil then
          transition:advance ()
          return
        end

        log:info (stream, string.format ("handling stream:%d (node:%d): %s",
            stream_id, node_id, tostring (stream_props["node.name"])))

        -- Make sure the stream nodes are still bound
        local stream_node_id = stream:get_node_bound_id ()
        if stream_node_id == Id.INVALID then
          transition:return_error ("stream's node is no longer bound")
          return;
        end
        local target_node_id = target:get_node_bound_id ()
        if target_node_id == Id.INVALID then
          transition:return_error ("target stream's node is no longer bound")
          return;
        end

        -- Figure out which stream is inpout and output
        local target_id = target.id
        local target_props = target:get_properties ()
        if stream:get_direction () == Direction.OUTPUT then
          out_stream = stream
          in_stream = target
          log:debug (stream, string.format (
              "playback link: stream:%d (node:%d) -> target:%d (node:%d)",
              stream_id, node_id, target_id, target_node_id))
        else
          out_stream = target
          in_stream = stream
          log:debug (stream, string.format (
              "capture link: stream:%d (node:%d) <- target:%d (node:%d)",
              stream_id, node_id, target_id, target_node_id))
        end

        -- Create stream link
        log:info (stream, string.format (
            "linking with target stream:%d (node:%d): %s",
            target_id, target_node_id, tostring (target_props["node.name"])))
        local stream_link = StreamLinkManager.create_link (out_stream, in_stream)

        -- Role-based links are activated by the media role rescan hooks,
        -- which apply priority based mixing, ducking and corking to them
        local is_role_link = stream_utils.is_role_based_link (stream_link)
        if is_role_link then
          log:debug (stream, "... role-based link, deferring activation")
          transition:advance ()
          return
        end

        -- Activate link
        stream_link:activate (Features.ALL, function (sl, e)
          if e ~= nil then
            transition:return_error (
                "failed to activate stream-link: " .. tostring (e))
          else
            log:info (stream, "linked with target successfully")
            transition:advance ()
          end
        end)
      end,
    },
  },
}:register ()
