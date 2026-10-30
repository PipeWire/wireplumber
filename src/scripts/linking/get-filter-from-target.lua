-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target stream has a smart filter.

stream_utils = require ("stream-utils")
futils = require ("filter-utils")

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/get-filter-from-target",
  after = { "linking/find-defined-target",
            "linking/find-filter-target",
            "linking/find-media-role-target",
            "linking/find-default-target",
            "linking/find-best-target" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream_om = source:call ("get-object-manager", "stream")
    local stream = event:get_subject ()
    local stream_props = stream:get_properties ()
    local stream_id = stream.id
    local node_id = stream:get_node_bound_id ()
    local target = event:get_data ("target")
    local is_defined_target = event:get_data ("is-defined-target") == true

    -- Bypass the hook if the target was not found or if it is role-based
    if target == nil or stream_utils.is_role_policy_target (stream, target) then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d): %s",
        stream_id, node_id, tostring (stream_props["node.name"])))

    local target_direction = stream_utils.get_target_direction (stream)

    -- Bypass the hook if the stream is a smart filter
    if futils.is_filter_smart (target_direction, stream) then
      return
    end

    local target_props = target:get_properties ()

    -- Bypass the hook if the target is an audio group
    if target_props ["session.audio-group"] ~= nil then
      return
    end

    -- Bypass targetable smart filters selected explicitly
    if is_defined_target and
        futils.is_filter_smart (target_direction, target) and
        not futils.is_filter_disabled (target_direction, target) and
        futils.is_filter_targetable (target_direction, target) then
      return
    end

    -- Get the filter from the given target if it exists, otherwise get the
    -- default filter, but only if the target was not defined
    local media_type = stream_utils.get_media_type (stream)
    local filter_target = futils.get_filter_from_target (
        target_direction, media_type, target, stream_om)
    if filter_target ~= nil then
      target = filter_target
      log:info (stream, "got filter for given target")
    elseif not is_defined_target then
      filter_target = futils.get_filter_from_target (
          target_direction, media_type, nil, stream_om)
      if filter_target ~= nil then
        target = filter_target
        log:info (stream, "got default filter for given target")
      end
    end

    if target == nil then
      return
    end

    -- Make sure the target can be linked and is compatible
    if not stream_utils.can_link (stream, target, stream_om) then
      log:debug (stream, "cannot link, ignoring filter target")
      return
    end

    local compatible, enc_only = stream:is_compatible (target)
    if not compatible then
      log:debug (stream, "not compatible, ignoring filter target")
      return
    end

    local target_new_props = target:get_properties ()
    local target_id = target.id
    local target_node_id = target:get_node_bound_id ()
    log:info (stream, string.format (
        "found filter target stream:%d (node:%d): %s",
        target_id, target_node_id, tostring (target_new_props["node.name"])))
    event:set_data ("target", target)
  end
}:register ()
