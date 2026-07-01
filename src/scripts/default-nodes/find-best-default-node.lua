-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-default-nodes")

nutils = require ("node-utils")
futils = require ("filter-utils")

SimpleEventHook {
  name = "default-nodes/find-best-default-node",
  after = { "default-nodes/find-selected-default-node",
            "default-nodes/find-stored-default-node" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    local def_node_type = props ["default-node.type"]
    local available_nodes = event:get_data ("available-nodes")
    local selected_prio = event:get_data ("selected-node-priority") or 0
    local selected_route_prio = event:get_data ("selected-route-priority") or 0
    local selected_node = event:get_data ("selected-node")

    -- A very high priority node is already selected, so we can skip this hook
    if selected_route_prio > 15000 then
      return
    end

    available_nodes = available_nodes and available_nodes:parse ()
    if not available_nodes then
      return
    end

    for _, node_props in ipairs (available_nodes) do
      -- Highest priority node wins
      local priority = nutils.get_session_priority (node_props)
      local route_priority = nutils.get_route_priority (node_props)
      local media_class = node_props ["media.class"]
      local node_name = node_props ["node.name"]

      -- Never consider sink nodes as best if audio.source is the def node type
      if media_class == "Audio/Sink" and def_node_type == "audio.source" then
        log:debug ("ignoring Audio/Sink node " .. tostring (node_name) .. " as best " .. def_node_type)
        goto skip_node
      end

      -- Never consider smart filters as default nodes
      local link_group = node_props ["node.link-group"]
      if link_group ~= nil then
        local direction = media_class:find("Source", 1, true) and "output" or "input"
        if futils.is_filter_smart (direction, link_group) then
          log:debug ("ignoring smart filter " .. tostring (node_name) .. " as best " .. def_node_type)
          goto skip_node
        end
      end

      if selected_node == nil or
          priority > selected_prio or
          (priority == selected_prio and route_priority > selected_route_prio)
          then
        selected_prio = priority
        selected_route_prio = route_priority
        selected_node = node_props ["node.name"]
      end

      ::skip_node::
    end

    event:set_data ("selected-node-priority", selected_prio)
    event:set_data ("selected-route-priority", selected_route_prio)
    event:set_data ("selected-node", selected_node)
  end
}:register ()
