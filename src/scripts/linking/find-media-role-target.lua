-- WirePlumber
--
-- Copyright © 2024 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Select the media role target

stream_utils = require ("stream-utils")
futils = require ("filter-utils")

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-media-role-target",
  after = { "linking/find-defined-target",
            "linking/find-audio-group-target",
            "linking/find-filter-target" },
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

    -- Bypass the hook if the target is already picked up or if the role is
    -- not defined
    local target = event:get_data ("target")
    local media_role = stream_props["media.role"]
    if target ~= nil or media_role == nil then
      return
    end

    local target_direction = stream_utils.get_target_direction (stream)

    -- Bypass the hook if the stream is a smart filter
    local link_group = stream_props["node.link-group"]
    if link_group ~= nil and
        futils.is_filter_smart (target_direction, link_group) then
      return
    end

    log:info (stream, string.format ("handling stream:%d (node:%d role:%s): %s",
        stream_id, node_id, media_role, tostring (stream_props["node.name"])))

    local streams_om = source:call ("get-object-manager", "stream")

    for target_stream in streams_om:iterate {
      type = "Stream",
      Constraint { "direction", "=", target_direction, type = "gobject" },
      Constraint { "device.intended-roles", "+", type = "wp" },
    } do
      -- Make sure the target stream is compatible
      local compatible, enc_only = stream:is_compatible (target_stream)
      if not compatible then
        goto skip_target
      end

      -- Make sure the intended-roles property is available
      local roles_json = target_stream:get_property ("device.intended-roles")
      if roles_json == nil then
        goto skip_target
      end

      -- Check if the target has the stream's media role as intended role
      local roles_table = Json.Raw (roles_json):parse ()
      for _, target_role in ipairs (roles_table) do
        if target_role == media_role and
            stream_utils.can_link (stream, target_stream) then
          target = target_stream
          break
        end
      end

      if target ~= nil then
        break
      end

      ::skip_target::
    end

    if target ~= nil then
      local target_props = target:get_properties ()
      local target_id = target.id
      local target_node_id = target:get_node_bound_id ()
      log:info (stream, string.format (
          "found media role target stream:%d (node:%d): %s",
          target_id, target_node_id, tostring (target_props["node.name"])))
      event:set_data ("target", target)
    else
      log:info (stream, "could not find media role target stream")
    end
  end
}:register ()
