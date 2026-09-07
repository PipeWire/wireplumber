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
    local selected_node = event:get_data ("selected-node")

    -- a node selected by an earlier hook carries no ranking of its own; the
    -- serials are set so that it keeps the selection whenever a candidate
    -- merely ties with it on the priority
    local selected = {
      priority = event:get_data ("selected-node-priority") or 0,
      group_serial = math.mininteger,
      route_priority = event:get_data ("selected-route-priority") or 0,
      serial = math.mininteger,
    }

    -- A very high priority node is already selected, so we can skip this hook
    if selected.priority > 15000 then
      return
    end

    available_nodes = available_nodes and available_nodes:parse ()
    if not available_nodes then
      return
    end

    for _, node_props in ipairs (available_nodes) do
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

      -- Highest ranking node wins; see nutils.compare_nodes ()
      local ranking = nutils.get_node_ranking (node_props)

      log:debug("considering " .. tostring(node_name) ..
          ": prio " .. tostring(ranking.priority) ..
          ", route_prio " .. tostring(ranking.route_priority))

      if selected_node == nil or nutils.compare_nodes (ranking, selected) then
        selected = ranking
        selected_node = node_name
      end

      ::skip_node::
    end

    log:debug("-> selected " .. tostring(selected_node) ..
          ": prio " .. tostring(selected.priority) ..
          ", route_prio " .. tostring(selected.route_priority))

    event:set_data ("selected-node-priority", selected.priority)
    event:set_data ("selected-route-priority", selected.route_priority)
    event:set_data ("selected-node", selected_node)
  end
}:register ()
