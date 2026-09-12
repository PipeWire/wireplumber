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

-- Number of times a stream is allowed to fail linking against the same target
-- before the hook gives up, so that a target that can never work does not get
-- retried on every rescan.
MAX_LINK_ATTEMPTS = 5

-- Per-stream record of the last failed target: [stream id] = { target, count }
failed_links = {}

SimpleEventHook {
  name = "linking/clear-stream-link-state",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-removed" },
    },
  },
  execute = function (event)
    local stream_id = event:get_subject ().id
    failed_links [stream_id] = nil
    stream_utils.linked_once [stream_id] = nil
  end
}:register ()

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

        -- Bypass the hook if the target was not found
        local target = event:get_data ("target")
        if target == nil then
          transition:advance ()
          return
        end

        log:info (stream, string.format ("handling stream:%d (node:%d): %s",
            stream_id, node_id, tostring (stream_props["node.name"])))

        -- Give up if we already failed to link this stream to this target
        local failed = failed_links [stream_id]
        if failed ~= nil and failed.target == target.id and
            failed.count >= MAX_LINK_ATTEMPTS then
          transition:return_error (string.format (
              "failed to link with target stream:%d %d times, not retrying",
              failed.target, failed.count))
          return
        end

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

        local target_id = target.id
        local target_props = target:get_properties ()
        if stream:get_direction () == Direction.OUTPUT then
          log:debug (stream, string.format (
              "playback link: stream:%d (node:%d) -> target:%d (node:%d)",
              stream_id, node_id, target_id, target_node_id))
        else
          log:debug (stream, string.format (
              "capture link: stream:%d (node:%d) <- target:%d (node:%d)",
              stream_id, node_id, target_id, target_node_id))
        end

        -- Create stream link
        log:info (stream, string.format (
            "linking with target stream:%d (node:%d): %s",
            target_id, target_node_id, tostring (target_props["node.name"])))
        local stream_link = StreamLinkManager.link (stream, target)
        if stream_link == nil then
          transition:return_error ("failed to create stream link")
          return
        end
        stream_utils.linked_once [stream_id] = true

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
            -- Destroy it, or it stays in the manager as a peer of both
            -- streams and the next rescan considers them linked
            StreamLinkManager.remove_link (sl)

            if failed ~= nil and failed.target == target_id then
              failed.count = failed.count + 1
            else
              failed_links [stream_id] = { target = target_id, count = 1 }
            end

            transition:return_error (
                "failed to activate stream-link: " .. tostring (e))
          else
            failed_links [stream_id] = nil
            log:info (stream, "linked with target successfully")
            transition:advance ()
          end
        end)
      end,
    },
  },
}:register ()
