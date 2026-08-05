-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if a default stream can be picked up as target from a default node.

stream_utils = require ("stream-utils")

log = Log.open_topic ("s-linking")

default_nodes = Plugin.find ("default-nodes-api")

SimpleEventHook {
  name = "linking/find-default-target",
  after = { "linking/find-defined-target",
            "linking/find-audio-group-target",
            "linking/find-filter-target",
            "linking/find-media-role-target",
            "linking/find-media-role-sink-target" },
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
    local target_mc = nil

    -- Bypass the hook if the target is already picked up
    local target = event:get_data ("target")
    if target ~= nil then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    if default_nodes ~= nil then
      -- Get the target node media class for this stream
      target_mc = stream_utils.get_target_media_class (stream)
      if target_mc ~= nil then
        local target_node_id = nil
        local streams_om = nil

        -- get the default node for the target node media class
        target_node_id = default_nodes:call ("get-default-node", target_mc)
        log:debug (stream, string.format ("default node: media_class:%s id:%d",
            target_mc, target_node_id))

        -- find the default target stream
        streams_om = source:call ("get-object-manager", "stream")
        for target_stream in streams_om:iterate { type = "Stream" } do
          local target_stream_node_id = target_stream:get_node_bound_id ()
          if target_stream_node_id ~= target_node_id then
            goto skip_target
          end

          -- Make sure the target stream can be linked
          if not stream_utils.can_link (stream, target_stream) then
            log:debug (stream, "... cannot link, ignoring default target")
            goto skip_target
          end

          -- Make sure target stream is compatible
          local compatible, enc_only = stream:is_compatible (target_stream)
          if not compatible then
            log:debug (stream, "... not compatible, ignoring default target")
            goto skip_target
          end

          -- use this target
          target = target_stream
          break;

          ::skip_target::
        end
      end
    else
      log:debug (stream, "default nodes API module not found")
    end

    if target ~= nil then
      -- set event target and continue
      local target_props = target:get_properties()
      local target_id = target.id
      local target_node_id = target:get_node_bound_id ()
      log:info (stream, string.format (
          "found default target stream:%d (node:%d): %s",
          target_id, target_node_id, tostring (target_props["node.name"])))
      event:set_data ("target", target)
    else
      -- continue with other hooks
      log:info (stream, "could not find default target stream")
    end
  end
}:register ()
