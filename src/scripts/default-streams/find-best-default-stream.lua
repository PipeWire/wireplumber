-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-default-streams")

nutils = require ("node-utils")
futils = require ("filter-utils")

SimpleEventHook {
  name = "default-streams/find-best-default-stream",
  after = { "default-streams/find-configured-default-stream",
            "default-streams/find-stored-default-stream" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-stream" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    local def_stream_type = props ["default-stream.type"]
    local available_streams = event:get_data ("available-streams")
    local selected_stream = event:get_data ("selected-stream")

    -- a stream selected by an earlier hook carries no ranking of its own; the
    -- serials are set so that it keeps the selection whenever a candidate
    -- merely ties with it on the priority
    local selected = {
      priority = event:get_data ("selected-stream-priority") or 0,
      group_serial = math.mininteger,
      route_priority = event:get_data ("selected-route-priority") or 0,
      serial = math.mininteger,
    }

    -- A very high priority stream is already selected, so we can skip this
    -- hook
    if selected.priority > 15000 then
      return
    end

    local available_stream_ids = available_streams and available_streams:parse ()
    if not available_stream_ids then
      return
    end

    local source = event:get_source ()
    local streams_om = source:call ("get-object-manager", "stream")
    for _, id in ipairs (available_stream_ids) do
      local stream = streams_om:lookup {
        type = "Stream",
        Constraint { "id", "=", id, type = "gobject" },
      }
      if stream == nil then
        goto skip_stream
      end

      local stream_props = stream:get_properties ()
      local shape = stream:get_shape ()
      local name = tostring (stream_props ["node.name"]) .. ":" ..
          tostring (stream:get_port_group ())

      -- Never consider sink streams as best if audio.source is the def stream
      -- type
      if shape == StreamShape.SINK and def_stream_type == "audio.source" then
        log:debug ("ignoring sink stream " .. name .. " as best " ..
            def_stream_type)
        goto skip_stream
      end

      -- Never consider smart filters as default streams
      if futils.is_filter_smart (stream:get_direction (), stream) then
        log:debug ("ignoring smart filter " .. name .. " as best " ..
            def_stream_type)
        goto skip_stream
      end

      -- Highest ranking stream wins; see nutils.compare_nodes ()
      local ranking = nutils.get_node_ranking (stream_props)

      log:debug ("considering " .. name ..
          ": prio " .. tostring (ranking.priority) ..
          ", route_prio " .. tostring (ranking.route_priority))

      if selected_stream == nil or nutils.compare_nodes (ranking, selected) then
        selected = ranking
        selected_stream = stream
      end

      ::skip_stream::
    end

    log:debug ("-> selected " .. (selected_stream and
          (tostring (selected_stream:get_property ("node.name")) .. ":" ..
              tostring (selected_stream:get_port_group ())) or "nil") ..
          ": prio " .. tostring (selected.priority) ..
          ", route_prio " .. tostring (selected.route_priority))

    event:set_data ("selected-stream-priority", selected.priority)
    event:set_data ("selected-route-priority", selected.route_priority)
    event:set_data ("selected-stream", selected_stream)
  end
}:register ()
