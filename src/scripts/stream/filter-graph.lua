-- WirePlumber
--
-- Copyright © 2025 The WirePlumber project contributors
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic("s-stream")

config = {}
config.rules = Conf.get_section_as_json ("stream.filter-graph.rules", Json.Array{})

-- Create the DynamicRules object
filter_graph_dr = DynamicRules ()

-- Add the rules from the configuration to make them dynamic
for _, rule in ipairs (config.rules:parse (1)) do
  filter_graph_dr:add_json_rule (Json.Raw (rule))
end

-- Keep track of filter-graph indices applied on each stream id and rule id.
stream_graph_indices = {}

function getStreamRuleGraphIndices (stream_id, rule_id)
  local stream_rules = stream_graph_indices[stream_id]
  if not stream_rules then
    stream_rules = {}
    stream_graph_indices[stream_id] = stream_rules
  end

  local rule_indices = stream_rules[rule_id]
  if not rule_indices then
    rule_indices = {}
    stream_rules[rule_id] = rule_indices
  end

  return stream_rules, rule_indices
end

function getNextStreamGraphIndex (stream_rules)
  local next_index = 0

  for _, rule_indices in pairs (stream_rules) do
    for _, index in ipairs (rule_indices) do
      if index >= next_index then
        next_index = index + 1
      end
    end
  end

  return next_index
end

function setStreamFilterGraphParams (stream, graph_params)
  stream:set_props_param { params = Pod.Struct (graph_params) }
end

function applyStreamFilterGraphs (stream, rule_id, value)
  local stream_rules, rule_indices =
      getStreamRuleGraphIndices (stream.id, rule_id)
  local start_index = getNextStreamGraphIndex (stream_rules)
  local graphs = value:parse (1)

  -- Build the graph params
  local graph_params = {}
  for idx, val in ipairs (graphs) do
    local index = tonumber(idx) - 1
    local graph_index = start_index + index
    local key = "audioconvert.filter-graph." .. tostring (graph_index)

    log:info (stream, "setting stream filter graph param '" .. key .. "' to: " .. val)

    table.insert(graph_params, key)
    table.insert(graph_params, val)
    table.insert(rule_indices, graph_index)
  end

  -- Set graphs
  setStreamFilterGraphParams (stream, graph_params)
end

function removeStreamFilterGraphs (stream, rule_id)
  local stream_rules = stream_graph_indices[stream.id]
  if not stream_rules then
    return
  end

  local rule_indices = stream_rules[rule_id]
  if not rule_indices or #rule_indices == 0 then
    return
  end

  local graph_params = {}
  for _, index in ipairs (rule_indices) do
    local key = "audioconvert.filter-graph." .. tostring (index)
    log:info (stream, "clearing stream filter graph param '" .. key .. "'")
    table.insert(graph_params, key)
    table.insert(graph_params, Pod.None ())
  end

  if #graph_params > 0 then
    setStreamFilterGraphParams (stream, graph_params)
  end

  stream_rules[rule_id] = nil
  if next (stream_rules) == nil then
    stream_graph_indices[stream.id] = nil
  end
end

-- Handle apply-actions to apply graph to stream
filter_graph_dr:connect ("apply-actions", function (dr, rule_id, action, value, stream)
  if action == "create-filter-graph" then
    local stream_name = stream:get_property ("node.name")
    log:info (stream, "applying filter-graphs from rule " ..
        tostring (rule_id) .. " on stream: " .. tostring (stream_name))
    applyStreamFilterGraphs (stream, rule_id, value)
  end
end)

-- Handle revert-actions signal to revert graph from stream
filter_graph_dr:connect ("revert-actions", function (dr, rule_id, action, value, stream)
  if action == "create-filter-graph" then
    local stream_name = stream:get_property ("node.name")
    log:info (stream, "removing filter-graphs from rule " ..
        tostring (rule_id) .. " on stream: " .. tostring (stream_name))
    removeStreamFilterGraphs (stream, rule_id)
  end
end)

SimpleEventHook {
  name = "stream/add-to-filter-graph-rules",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "library.name", "=", "audioconvert/libspa-audioconvert", type = "pw" },
    },
  },
  execute = function(event)
    local stream = event:get_subject ()
    filter_graph_dr:add_object (stream)
  end
}:register()

SimpleEventHook {
  name = "stream/remove-from-filter-graph-rules",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-removed" },
      Constraint { "event.stream.type", "=", "audio" },
      Constraint { "library.name", "=", "audioconvert/libspa-audioconvert", type = "pw" },
    },
  },
  execute = function(event)
    local stream = event:get_subject ()
    filter_graph_dr:remove_object (stream)
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
