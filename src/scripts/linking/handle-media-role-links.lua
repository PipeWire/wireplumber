-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Rescan role-based stream links, applying priority based mixing, ducking
-- and corking between them.

stream_utils = require ("stream-utils")

log = Log.open_topic ("s-linking")

-- highest priority stream link seen so far, indexed by target media class
priority_media_role_link = {}

function get_priority (device_stream)
  local device_stream_props = device_stream:get_properties ()
  return device_stream_props:get_int ("policy.role-based.priority") or 0
end

-- Gather all the info needed to handle a stream link, or nil if the link is
-- not role-based (e.g. its target is a monitor stream)
function get_link_info (stream_link)
  local is_role, device_stream, media_role_stream = stream_utils.is_role_based_link (stream_link)
  if not is_role then
    return nil
  end

  return {
    link = stream_link,
    device_stream = device_stream,
    media_role_stream = media_role_stream,
    media_class = device_stream:get_property ("media.class"),
    priority = get_priority (device_stream),
    plugged = media_role_stream:get_creation_time (),
  }
end

function get_action (pmrl, info)
  if pmrl == nil then
    return "mix"
  end

  local pmrl_props = pmrl.device_stream:get_properties ()
  if pmrl.priority == info.priority then
    return pmrl_props ["policy.role-based.action.same-priority"] or "mix"
  else
    return pmrl_props ["policy.role-based.action.lower-priority"] or "mix"
  end
end

function set_priority_media_role_link (media_class, info)
  priority_media_role_link [media_class] = info
  if info then
    log:debug (string.format (
        "update priority link media role(\"%s\") priority(%d)",
        tostring (info.media_role_stream:get_property ("media.role")),
        info.priority))
  else
    log:debug ("clear priority media role")
  end
end

function update_priority_media_role_link (info)
  local pmrl = priority_media_role_link [info.media_class]
  if pmrl == nil or info.priority > pmrl.priority or
      (info.priority == pmrl.priority and info.plugged >= pmrl.plugged) then
    set_priority_media_role_link (info.media_class, info)
  end
end

-- if the removed link happens to be the priority one, clear it and find the
-- next best link with the same target media class
function clear_priority_media_role_link (links_om, removed_info)
  local pmrl = priority_media_role_link [removed_info.media_class]
  if pmrl == nil or pmrl.link ~= removed_info.link then
    return
  end

  local best = nil
  for stream_link in links_om:iterate { type = "StreamLink" } do
    if stream_link == removed_info.link then
      goto continue
    end

    local info = get_link_info (stream_link)
    if info == nil or info.media_class ~= removed_info.media_class then
      goto continue
    end

    if best == nil or info.priority > best.priority or
        (info.priority == best.priority and info.plugged > best.plugged) then
      best = info
    end

    ::continue::
  end

  set_priority_media_role_link (removed_info.media_class, best)
end

function get_suspend_playback (metadata_om)
  local metadata = metadata_om:lookup {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
  if metadata then
    local value = metadata:find (0, "suspend.playback")
    if value then
      return value == "1"
    end
  end
  return false
end

function set_volume (streams_om, device_stream, level)
  -- apply volume control on the companion stream of the loopback, instead of
  -- the device stream itself, as it simplifies ducking and restoration
  -- regardless of the link direction
  local other_stream = stream_utils.get_filter_peer (device_stream, streams_om)
  if other_stream == nil then
    return
  end

  log:info (string.format ("... %s volume of media role node \"%s\" to %f",
      level < 1.0 and "duck" or "restore",
      tostring (other_stream:get_property ("node.name")), level))

  other_stream:set_props_param { volume = Pod.Float (level) }
end

function restore_volume (streams_om, device_stream)
  set_volume (streams_om, device_stream, 1.0)
end

function duck_volume (streams_om, device_stream)
  local duck_level = Settings.get_float ("linking.role-based.duck-level")
  set_volume (streams_om, device_stream, duck_level)
end

-- Rescan the given media classes, applying priority based mixing, ducking
-- and corking, and advance the transition once all pending activations
-- (if any) have completed.
function rescan_media_classes (source, media_classes, transition)
  local streams_om = source:call ("get-object-manager", "stream")
  local links_om = source:call ("get-object-manager", "stream-link")
  local metadata_om = source:call ("get-object-manager", "metadata")
  local suspend = get_suspend_playback (metadata_om)
  local pending_activations = 0

  local function onMediaRoleLinkActivated (stream_link, e)
    if e ~= nil then
      log:warning (stream_link, "failed to activate media role link: " .. tostring (e))
      StreamLinkManager.remove_link (stream_link)
    else
      log:info (stream_link, "media role link activated successfully")
    end

    -- advance only when all pending activations are completed
    pending_activations = pending_activations - 1
    if pending_activations <= 0 then
      log:info ("All media role links activated")
      transition:advance ()
    end
  end

  for media_class in pairs (media_classes) do
    local pmrl = priority_media_role_link [media_class]
    local pmrl_seen = false

    for stream_link in links_om:iterate { type = "StreamLink" } do
      local info = get_link_info (stream_link)
      if info == nil or info.media_class ~= media_class then
        goto continue
      end

      -- deactivate all links if suspend playback metadata is present
      if suspend then
        stream_link:deactivate (Features.ALL)
      end

      if pmrl ~= nil and info.link == pmrl.link then
        pmrl_seen = true
        restore_volume (streams_om, info.device_stream)
        goto continue
      end

      local action = get_action (pmrl, info)
      local active = stream_link:get_active_features () ~= 0

      if action == "cork" then
        if active then
          stream_link:deactivate (Features.ALL)
        end
      elseif action == "mix" then
        if not active and not suspend then
          pending_activations = pending_activations + 1
          stream_link:activate (Features.ALL, onMediaRoleLinkActivated)
        end
        restore_volume (streams_om, info.device_stream)
      elseif action == "duck" then
        if not active and not suspend then
          pending_activations = pending_activations + 1
          stream_link:activate (Features.ALL, onMediaRoleLinkActivated)
        end
        duck_volume (streams_om, info.device_stream)
      else
        log:warning (stream_link, "Unknown action: " .. tostring (action))
      end

      ::continue::
    end

    if pmrl ~= nil and not pmrl_seen then
      pending_activations = pending_activations + 1
      pmrl.link:activate (Features.ALL, onMediaRoleLinkActivated)
      restore_volume (streams_om, pmrl.device_stream)
    end
  end

  -- just advance transition if no pending activations are needed
  if pending_activations <= 0 then
    log:debug ("All media role links rescanned")
    transition:advance ()
  end
end

AsyncEventHook {
  name = "linking/handle-media-role-links-on-added",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-link-added" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local source = event:get_source ()
        local stream_link = event:get_subject ()

        -- Make sure we only handle media role stream links
        local info = get_link_info (stream_link)
        if info == nil then
          transition:advance ()
          return
        end

        log:debug (stream_link, "Rescanning on media role link added...")

        -- Update priority
        update_priority_media_role_link (info)

        -- Rescan
        local media_classes = {
          [info.media_class] = true
        }
        rescan_media_classes (source, media_classes, transition)
      end,
    },
  },
}:register ()

AsyncEventHook {
  name = "linking/handle-media-role-links-on-removed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-link-removed" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local source = event:get_source ()
        local stream_link = event:get_subject ()

        -- Make sure we only handle media role stream links
        local info = get_link_info (stream_link)
        if info == nil then
          transition:advance ()
          return
        end

        log:debug (stream_link, "Rescanning on media role link removed...")

        -- Clear priority
        local links_om = source:call ("get-object-manager", "stream-link")
        clear_priority_media_role_link (links_om, info)

        -- Rescan
        local media_classes = {
          [info.media_class] = true
        }
        rescan_media_classes (source, media_classes, transition)
      end,
    },
  },
}:register ()

AsyncEventHook {
  name = "linking/handle-media-role-links-on-metadata-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "=", "suspend.playback" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local source = event:get_source ()
        local links_om = source:call ("get-object-manager", "stream-link")
        local media_classes = {}

        for stream_link in links_om:iterate { type = "StreamLink" } do
          local info = get_link_info (stream_link)
          if info ~= nil then
            media_classes [info.media_class] = true
          end
        end

        log:debug ("Rescanning on default metadata suspend.playback changed...")

        -- rescan
        rescan_media_classes (source, media_classes, transition)
      end,
    },
  },
}:register ()
