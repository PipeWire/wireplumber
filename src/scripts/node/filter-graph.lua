-- WirePlumber
--
-- Copyright © 2025 The WirePlumber project contributors
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic("s-node")

config = {}
config.rules = Conf.get_section_as_json ("node.filter-graph.rules", Json.Array{})

-- Create the DynamicRules object
filter_graph_dr = DynamicRules ()

-- Add the rules from the configuration to make them dynamic
for _, rule in ipairs (config.rules:parse (1)) do
  filter_graph_dr:add_json_rule (Json.Raw (rule))
end

-- Keep track of filter-graph indices applied on each node id and rule id.
node_graph_indices = {}

function getNodeRuleGraphIndices (node_id, rule_id)
  local node_rules = node_graph_indices[node_id]
  if not node_rules then
    node_rules = {}
    node_graph_indices[node_id] = node_rules
  end

  local rule_indices = node_rules[rule_id]
  if not rule_indices then
    rule_indices = {}
    node_rules[rule_id] = rule_indices
  end

  return node_rules, rule_indices
end

function getNextNodeGraphIndex (node_rules)
  local next_index = 0

  for _, rule_indices in pairs (node_rules) do
    for _, index in ipairs (rule_indices) do
      if index >= next_index then
        next_index = index + 1
      end
    end
  end

  return next_index
end

function setNodeFilterGraphParams (node, graph_params)
  local pod = Pod.Object {
    "Spa:Pod:Object:Param:Props", "Props",
    params = Pod.Struct (graph_params)
  }
  node:set_params("Props", pod)
end

function applyNodeFilterGraphs (node, rule_id, value)
  local node_rules, rule_indices = getNodeRuleGraphIndices (node.id, rule_id)
  local start_index = getNextNodeGraphIndex (node_rules)
  local graphs = value:parse (1)

  -- Build the graph params
  local graph_params = {}
  for idx, val in ipairs (graphs) do
    local index = tonumber(idx) - 1
    local graph_index = start_index + index
    local key = "audioconvert.filter-graph." .. tostring (graph_index)

    log:info (node, "setting node filter graph param '" .. key .. "' to: " .. val)

    table.insert(graph_params, key)
    table.insert(graph_params, val)
    table.insert(rule_indices, graph_index)
  end

  -- Set graphs
  setNodeFilterGraphParams (node, graph_params)
end

function removeNodeFilterGraphs (node, rule_id)
  local node_rules = node_graph_indices[node.id]
  if not node_rules then
    return
  end

  local rule_indices = node_rules[rule_id]
  if not rule_indices or #rule_indices == 0 then
    return
  end

  local graph_params = {}
  for _, index in ipairs (rule_indices) do
    local key = "audioconvert.filter-graph." .. tostring (index)
    log:info (node, "clearing node filter graph param '" .. key .. "'")
    table.insert(graph_params, key)
    table.insert(graph_params, Pod.None ())
  end

  if #graph_params > 0 then
    setNodeFilterGraphParams (node, graph_params)
  end

  node_rules[rule_id] = nil
  if next (node_rules) == nil then
    node_graph_indices[node.id] = nil
  end
end

-- Handle apply-actions to apply graph to node
filter_graph_dr:connect ("apply-actions", function (dr, rule_id, action, value, node)
  if action == "create-filter-graph" then
    local node_name = node.properties["node.name"]
    log:info (node, "applying filter-graphs from rule " ..
        tostring (rule_id) .. " on node: " .. tostring (node_name))
    applyNodeFilterGraphs (node, rule_id, value)
  end
end)

-- Handle revert-actions signal to revert graph from node
filter_graph_dr:connect ("revert-actions", function (dr, rule_id, action, value, node)
  if action == "create-filter-graph" then
    local node_name = node.properties["node.name"]
    log:info (node, "removing filter-graphs from rule " ..
        tostring (rule_id) .. " on node: " .. tostring (node_name))
    removeNodeFilterGraphs (node, rule_id)
  end
end)

SimpleEventHook {
  name = "node/add-to-filter-graph-rules",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "library.name", "=", "audioconvert/libspa-audioconvert", type = "pw" },
    },
  },
  execute = function(event)
    local node = event:get_subject ()
    filter_graph_dr:add_object (node)
  end
}:register()

SimpleEventHook {
  name = "node/remove-from-filter-graph-rules",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-removed" },
      Constraint { "library.name", "=", "audioconvert/libspa-audioconvert", type = "pw" },
    },
  },
  execute = function(event)
    local node = event:get_subject ()
    filter_graph_dr:remove_object (node)
  end
}:register()

-- Finish script when filter-graph dynamic rules are activated
Script.async_activation = true

filter_graph_dr:activate (Features.ALL, function (dr, e)
  if e then
    Script:finish_activation_with_error (
        "failed to activate the filter-graph dynamic rules: " .. tostring (e))
  else
    log:info ("filter-graph dynamic rules ready")
    Script:finish_activation ()
  end
end)
