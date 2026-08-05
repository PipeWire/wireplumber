-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check the properties of target, which indicate if it is not available for
-- linking. If no target is available, send down an error to the corresponding
-- client.

stream_utils = require ("stream-utils")

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/prepare-stream-link",
  after = { "linking/find-defined-target",
            "linking/find-best-target",
            "linking/get-filter-from-target" },
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
    local must_passthrough = stream:get_supported_enc_format_count() > 0 and
        stream:get_supported_raw_format_count() == 0
    local target = event:get_data ("target")

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    -- Don't do anything if already linked to proper target
    for stream_peer in StreamLinkManager.iterate_peers (stream) do
      if target ~= nil and stream_peer.id == target.id then
        log:debug (stream, "... already linked to proper target")

        -- Check this also here, in case in default targets changed
        if Settings.get_boolean ("linking.follow-default-target") and
            event:get_data ("is-defined-target") == true then
          stream_utils.check_follow_default (stream, target)
        end

        target = nil
        goto done
      end
    end

    -- Unlink the stream from previous target if it is linked, unless the
    -- dont-reconnect property is set
    if StreamLinkManager.get_peers_count (stream) > 0 then
      local dont_reconnect = stream_props:get_boolean ("node.dont-reconnect")
      if dont_reconnect then
        log:debug (stream, "... stream has dont-reconnect")
        target = nil
        goto done
      end

      StreamLinkManager.unlink (stream)
      log:debug (stream, "... moving to new target")
    end

    -- If the target is already linked with another stream, make sure the target
    -- and the stream are not exclusive.
    if target ~= nil and StreamLinkManager.get_peers_count (target) > 0 then
      if target:is_exclusive () or stream_props:get_boolean ("node.exclusive") then
        log:debug (stream, "... target is already linked exclusively")
        target = nil
      end
    end

    if target == nil then
      -- Dont send client error if linger node
      local linger = stream_props:get_boolean ("node.linger")
      if linger then
        log:debug (stream, "... node linger")
        goto done
      end

      -- Send client error if stream has client ID
      local dont_reconnect = stream_props:get_boolean ("node.dont-reconnect")
      local message = dont_reconnect and "not reconnecting" or "target not found"
      if stream_utils.send_client_error (stream, source, -2, message) then
        log:debug (stream, "... sent client error with message: " .. message)
      end

      -- request client to destroy node
      if dont_reconnect then
        local node = stream:get_node ()
        node:request_destroy ()
        log:info (stream, "... requested destroy node")
      end
    end

    ::done::
    if target ~= nil then
      local target_id = target.id
      local target_node_id = target:get_node_bound_id ()
      local target_props = target:get_properties ()
      log:info (stream, string.format (
          "prepared target stream:%d (node:%d exclusive:%s): %s",
          target_id, target_node_id, tostring (target:is_exclusive ()),
          tostring (target_props["node.name"])))
    else
      log:info (stream, "not linking")
    end
    event:set_data ("target", target)
  end
}:register ()
