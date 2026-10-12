-- WirePlumber
--
-- Copyright © 2020-2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Rescans the graph by pushing new select-target events for all streams
-- that need to be linked.

stream_utils = require ("stream-utils")
futils = require ("filter-utils")

log = Log.open_topic ("s-linking")

handles = {}
handles.rescan_enabled = true
handles.timeout_source = nil

function handle_stream (source, stream)
  -- Only handle subject streams
  if stream:get_kind () ~= StreamKind.SUBJECT then
    return
  end

  local stream_props = stream:get_properties ()
  local stream_id = stream.id
  local node_id = stream:get_node_bound_id ()

  -- Skip smart filters that are disabled
  local link_group = stream_props ["node.link-group"]
  if link_group ~= nil then
    local direction = stream_utils.get_target_direction (stream)
    if futils.is_filter_smart (direction, link_group) and
        futils.is_filter_disabled (direction, link_group) then
      log:debug (stream, string.format (
          "ignoring stream:%d (node:%d) as smart filter is disabled",
          stream_id, node_id))
      return
    end
  end

  -- Check if we need to link this stream at all
  local autoconnect = stream_props:get_boolean ("node.autoconnect")
  if not autoconnect then
    log:info (stream, string.format (
        "ignoring stream:%d (node:%d) as autoconnect is not enabled",
        stream_id, node_id))
    return
  end

  -- Push event to find stream target and link
  source:call ("push-event", "select-target", stream, nil)
end

function handle_streams (source)
  local om = source:call ("get-object-manager", "stream")
  for stream in om:iterate { type = "Stream" } do
    handle_stream (source, stream)
  end
end

-- handle_stream() skips a smart filter that is disabled, so the links it made
-- while it was enabled have to be removed here
function unlink_disabled_filters (source)
  local om = source:call ("get-object-manager", "stream")
  for stream in om:iterate {
      type = "Stream",
      Constraint { "node.link-group", "+" },
  } do
    local link_group = stream:get_properties () ["node.link-group"]
    local direction = stream_utils.get_target_direction (stream)
    if futils.is_filter_smart (direction, link_group) and
        futils.is_filter_disabled (direction, link_group) and
        StreamLinkManager.unlink (stream) then
      log:info (stream, string.format (
          "unlinked stream:%d (node:%d) as smart filter is disabled",
          stream.id, stream:get_node_bound_id ()))
    end
  end
end

SimpleEventHook {
  name = "linking/rescan",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "rescan-for-linking" },
    },
  },
  execute = function (event)
    local source = event:get_source ()

    log:info ("rescanning for linking...")

    unlink_disabled_filters (source)
    handle_streams (source)
  end
}:register ()

SimpleEventHook {
  name = "linking/rescan-trigger",
  interests = {
    -- on device Route and EnumRoute params changed
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "c", "Route", "EnumRoute" },
    },
    -- on default metadata changed
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "c", "default.audio.source",
          "default.audio.sink", "default.video.source" },
    },
    -- on filters metadata changed
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "filters" },
    },
  },
  execute = function (event)
    if handles.rescan_enabled then
      local source = event:get_source ()
      source:call ("schedule-rescan", "linking")
    end
  end
}:register ()

SimpleEventHook {
  name = "linking/stream-added",
  before = "linking/rescan-trigger",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    -- clear timeout source, if any
    if handles.timeout_source ~= nil then
      handles.timeout_source:destroy ()
      handles.timeout_source = nil
    end

    -- Always enable rescan when any stream is added
    handles.rescan_enabled = true
  end
}:register ()

-- Link a new stream right away, without waiting for the rescan. A smart filter
-- is left out because enabling it moves the streams that are already linked,
-- which only the rescan does. This is also the only thing that links a stream
-- when hooks.linking.rescan-on-stream is disabled.
--
-- select-target has a higher priority than stream-added, so pushing it here
-- suspends the remaining stream-added hooks until the stream is linked. This
-- hook therefore has to run after every hook that prepares the graph for
-- linking.
SimpleEventHook {
  name = "linking/stream-added-immediate",
  after = { "linking/stream-added",
            "lib/audio-group-utils/create-audio-group-loopback" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local stream = event:get_subject ()
    local link_group = stream:get_properties () ["node.link-group"]

    -- Don't handle immediately if this stream has node.link-group, because it
    -- could affect others.
    if link_group ~= nil then
      return
    end

    -- Let the rescan link this, as filters may need to be relinked first
    if futils.is_rescan_pending () then
      source:call ("schedule-rescan", "linking")
      return
    end

    handle_stream (source, stream)
  end
}:register ()

-- Stop rescan for 2 seconds if a bluez stream was removed. This avoids audio
-- being played on internal nodes for a few seconds while the BT device is
-- switching profiles.
SimpleEventHook {
  name = "linking/bluez-stream-removed",
  before = "linking/rescan-trigger",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-removed" },
      Constraint { "device.api", "=", "bluez5" },
    },
  },
  execute = function (event)
    local source = event:get_source ()

    -- clear timeout source, if any
    if handles.timeout_source ~= nil then
      handles.timeout_source:destroy ()
      handles.timeout_source = nil
    end

    -- disable rescan
    handles.rescan_enabled = false

    -- re-enable rescan after 2 seconds
    handles.timeout_source = Core.timeout_add (2000, function()
      handles.timeout_source = nil
      handles.rescan_enabled = true
      source:call ("schedule-rescan", "linking")
    end)
  end
}:register ()

function handleMoveSetting (enable)
  if (not handles.move_hook) and (enable == true) then
    handles.move_hook = SimpleEventHook {
      name = "linking/rescan-trigger-on-target-metadata-changed",
      interests = {
        EventInterest {
          Constraint { "event.type", "=", "metadata-changed" },
          Constraint { "metadata.name", "=", "default" },
          Constraint { "event.subject.key", "c", "target.object", "target.node" },
        },
        EventInterest {
          Constraint { "event.type", "=", "metadata-changed" },
          Constraint { "metadata.name", "=", "default" },
          Constraint { "event.subject.key", "#", "target.stream.*" },
        },
      },
      execute = function (event)
        if handles.rescan_enabled then
          local source = event:get_source ()
          source:call ("schedule-rescan", "linking")
        end
      end
    }
    handles.move_hook:register()
  elseif (handles.move_hook) and (enable == false) then
    handles.move_hook:remove ()
    handles.move_hook = nil
  end
end

Settings.subscribe ("linking.allow-moving-streams", function ()
  handleMoveSetting (Settings.get_boolean ("linking.allow-moving-streams"))
end)
handleMoveSetting (Settings.get_boolean ("linking.allow-moving-streams"))
