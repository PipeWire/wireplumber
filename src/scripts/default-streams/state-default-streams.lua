-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- the script states the default streams from the user preferences, it has hooks
-- which stores the user preferences(it stores not just the current preference
-- but all the previous preferences) in to the state file, retrieves them from
-- state file during the bootup, finally it has a hook which finds a default
-- stream out of the user preferences

log = Log.open_topic ("s-default-streams")


-- the state storage
state = nil
state_table = nil

find_stored_default_stream_hook = SimpleEventHook {
  name = "default-streams/find-stored-default-stream",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-stream" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    local available_streams = event:get_data ("available-streams")
    local selected_prio = event:get_data ("selected-stream-priority") or 0
    local selected_stream = nil

    local available_stream_ids =
        available_streams and available_streams:parse ()
    if not available_stream_ids then
      return
    end

    local stored = collectStored (props ["default-stream.type"])

    -- Check if any of the available streams matches any of the configured,
    -- which matches any stream of the node if it has no stream
    local source = event:get_source ()
    local streams_om = source:call ("get-object-manager", "stream")
    for _, id in ipairs (available_stream_ids) do
      local stream = streams_om:lookup {
        type = "Stream",
        Constraint { "id", "=", id, type = "gobject" },
      }
      if stream == nil then
        goto next_stream
      end

      for i, v in ipairs (stored) do
        if stream:get_property ("node.name") == v.name and
            (v.stream == nil or stream:get_port_group () == v.stream) then
          -- the position in the stack alone decides; priority.session is
          -- deliberately left out so that it cannot override the user's history
          local priority = 20001 - i

          if priority > selected_prio then
            selected_prio = priority
            selected_stream = stream
          end

          break
        end
      end

      ::next_stream::
    end

    if selected_stream then
      event:set_data ("selected-stream-priority", selected_prio)
      event:set_data ("selected-stream", selected_stream)
    end
  end
}

store_configured_default_streams_hook = SimpleEventHook {
  name = "default-streams/store-configured-default-streams",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "c", "default.configured.audio.sink",
          "default.configured.audio.source", "default.configured.video.source"
      },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    -- get the part after "default.configured." (= 19 chars)
    local def_stream_type = props ["event.subject.key"]:sub (20)
    local new_value = props ["event.subject.value"]
    local new_stored = {}

    if new_value then
      local parsed = Json.Raw (new_value):parse ()
      new_value = { ["name"] = parsed ["name"], ["stream"] = parsed ["stream"] }
    end

    if new_value and new_value.name then
      local stored = collectStored (def_stream_type)
      local pos = #stored + 1

      -- find if the current configured value is already in the stack
      for i, v in ipairs (stored) do
        if v.name == new_value.name and v.stream == new_value.stream then
          pos = i
          break
        end
      end

      -- insert at the top and shift the remaining to fill the gap
      new_stored [1] = new_value
      if pos > 1 then
        table.move (stored, 1, pos-1, 2, new_stored)
      end
      if pos < #stored then
        table.move (stored, pos+1, #stored, pos+1, new_stored)
      end
    end

    updateStored (def_stream_type, new_stored)
  end
}

-- set initial values
metadata_added_hook = SimpleEventHook {
  name = "default-streams/metadata-added",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-added" },
      Constraint { "metadata.name", "=", "default" },
    },
  },
  execute = function (event)
    local types = { "audio.sink", "audio.source", "video.source" }
    local source = event:get_source ()
    local om = source:call ("get-object-manager", "metadata")
    local metadata = om:lookup { Constraint { "metadata.name", "=", "default" } }

    for _, t in ipairs (types) do
      local v = state_table ["default.configured." .. t]
      if v then
        metadata:set (0, "default.configured." .. t, "Spa:String:JSON", v)
      end
    end
  end
}

-- Collect all the previously configured streams from the state file, as
-- tables with the node name as "name" and the port group as "stream"
function collectStored (def_stream_type)
  local stored = {}
  local key_base = "default.configured." .. def_stream_type
  local key = key_base

  local index = 0
  repeat
    local v = state_table [key]
    if v then
      local parsed = Json.Raw (v):parse ()
      table.insert (stored,
          { ["name"] = parsed ["name"], ["stream"] = parsed ["stream"] })
    end
    key = key_base .. "." .. tostring (index)
    index = index + 1
  until v == nil

  return stored
end

-- Store the given streams in the state file
function updateStored (def_stream_type, stored)
  local key_base = "default.configured." .. def_stream_type
  local key = key_base

  local index = 0
  for _, v in ipairs (stored) do
    state_table [key] = Json.Object (v):to_string ()
    key = key_base .. "." .. tostring (index)
    index = index + 1
  end

  -- erase the rest, if any
  repeat
    local v = state_table [key]
    state_table [key] = nil
    key = key_base .. "." .. tostring (index)
    index = index + 1
  until v == nil

  state:save_after_timeout (state_table)
end

function toggleState (enable)
  if enable and not state then
    state = State ("default-streams")
    state_table = state:load ()
    find_stored_default_stream_hook:register ()
    store_configured_default_streams_hook:register ()
    metadata_added_hook:register ()
  elseif not enable and state then
    state = nil
    state_table = nil
    find_stored_default_stream_hook:remove ()
    store_configured_default_streams_hook:remove ()
    metadata_added_hook:remove ()
  end
end

Settings.subscribe ("node.restore-default-targets", function ()
  toggleState (Settings.get_boolean ("node.restore-default-targets"))
end)
toggleState (Settings.get_boolean ("node.restore-default-targets"))
