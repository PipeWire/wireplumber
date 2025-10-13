-- WirePlumber
--
-- Copyright © 2025 The WirePlumber project contributors
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic("s-node")

config = {}
config.rules = Conf.get_section_as_json ("node.filter-graph.rules", Json.Array{})

function setNodeFilterGraphParams (node, graph_params)
  local pod = Pod.Object {
    "Spa:Pod:Object:Param:Props", "Props",
    params = Pod.Struct (graph_params)
  }
  node:set_params("Props", pod)
end


SimpleEventHook {
  name = "node/create-filter-graph",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-added" },
      Constraint { "library.name", "=", "audioconvert/libspa-audioconvert", type = "pw" },
    },
  },
  execute = function(event)
    local node = event:get_subject()

    JsonUtils.match_rules (config.rules, node.properties, function (action, value)

      if action == "create-filter-graph" then
        local graphs = value:parse (1)

	local graph_params = {}
        for idx, val in ipairs (graphs) do
	  local index = tonumber(idx) - 1
	  local key = "audioconvert.filter-graph." .. tostring (index)

	  log:info (node, "setting node filter graph param '" .. key .. "' to: " .. val)

	  table.insert(graph_params, key)
	  table.insert(graph_params, val)
	end

        setNodeFilterGraphParams (node, graph_params)
      end
    end)
  end
}:register()
