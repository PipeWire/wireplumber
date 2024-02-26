-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- the script states the default nodes from the user preferences, it has hooks
-- which stores the user preferences(it stores not just the current preference
-- but all the previous preferences) in to the state file, retrives them from
-- state file during the bootup, finally it has a hook which finds a default
-- node out of the user preferences

log = Log.open_topic ("s-default-nodes")

-- the state storage
state = nil
state_table = nil

find_stored_default_node_hook = SimpleEventHook {
  name = "default-nodes/find-stored-default-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    local available_nodes = event:get_data ("available-nodes")
    local selected_prio = event:get_data ("selected-node-priority") or 0
    local selected_node = event:get_data ("selected-node")

    available_nodes = available_nodes and available_nodes:parse ()
    if not available_nodes then
      return
    end

    local stored = collectStored (props ["default-node.type"])

    -- Check if any of the available nodes matches any of the configured
    for _, node_props in ipairs (available_nodes) do
      local name = node_props ["node.name"]

      for i, v in ipairs (stored) do
        if name == v then
          local priority = node_props ["priority.session"]
          priority = math.tointeger (priority) or 0
          priority = priority + 20001 - i

          if priority > selected_prio then
            selected_prio = priority
            selected_node = name
          end

          break
        end
      end
    end

    if selected_node then
      event:set_data ("selected-node-priority", selected_prio)
      event:set_data ("selected-node", selected_node)
    end
  end
}

store_configured_default_nodes_hook = SimpleEventHook {
  name = "default-nodes/store-configured-default-nodes",
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
    local def_node_type = props ["event.subject.key"]:sub (20)
    local new_value = props ["event.subject.value"]
    local new_stored = {}

    if new_value then
      new_value = Json.Raw (new_value):parse () ["name"]
    end

    if new_value then
      local stored = collectStored (def_node_type)
      local pos = #stored + 1

      -- find if the curent configured value is already in the stack
      for i, v in ipairs (stored) do
        if v == new_value then
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

    updateStored (def_node_type, new_stored)
  end
}

-- set initial values
metadata_added_hook = SimpleEventHook {
  name = "default-nodes/metadata-added",
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
        metadata:set (0, "default.configured." .. t, "Spa:String:JSON",
                      Json.Object { ["name"] = v }:to_string ())
      end
    end
  end
}

-- Collect all the previously configured node names from the state file
function collectStored (def_node_type)
  local stored = {}
  local key_base = "default.configured." .. def_node_type
  local key = key_base

  local index = 0
  repeat
    local v = state_table [key]
    table.insert (stored, v)
    key = key_base .. "." .. tostring (index)
    index = index + 1
  until v == nil

  return stored
end

-- Store the given node names in the state file
function updateStored (def_node_type, stored)
  local key_base = "default.configured." .. def_node_type
  local key = key_base

  local index = 0
  for _, v in ipairs (stored) do
    state_table [key] = v
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
    state = State ("default-nodes")
    state_table = state:load ()
    find_stored_default_node_hook:register ()
    store_configured_default_nodes_hook:register ()
    metadata_added_hook:register ()
  elseif not enable and state then
    state = nil
    state_table = nil
    find_stored_default_node_hook:remove ()
    store_configured_default_nodes_hook:remove ()
    metadata_added_hook:remove ()
  end
end

Settings.subscribe ("node.restore-default-targets", function ()
  toggleState (Settings.get_boolean ("node.restore-default-targets"))
end)
toggleState (Settings.get_boolean ("node.restore-default-targets"))
