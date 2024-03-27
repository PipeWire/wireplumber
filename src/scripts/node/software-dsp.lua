-- WirePlumber
--
-- Copyright © 2022-2023 The WirePlumber project contributors
--    @author Dmitry Sharshakov <d3dx12.xx@gmail.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic("s-node")

config = {}
config.rules = Conf.get_section_as_json("node.software-dsp.rules", Json.Array{})

-- TODO: port from Obj Manager to Hooks
nodes_om = ObjectManager {
  Interest { type = "node" },
}

clients_om = ObjectManager {
  Interest { type = "client" }
}

filter_nodes = {}
hidden_nodes = {}

nodes_om:connect("object-added", function (om, node)
  JsonUtils.match_rules (config.rules, node.properties, function (action, value)
    if action == "create-filter" then
      log:debug("DSP rule found for " .. node.properties["node.name"])
      local props = value:parse (1)

      if props["filter-graph"] then
        log:debug("Loading filter graph for " .. node.properties["node.name"])
        filter_nodes[node.properties["object.id"]] = LocalModule("libpipewire-module-filter-chain", props["filter-graph"], {})
      end

      if props["hide-parent"] then
        log:debug("Setting permissions to '-' on " .. node.properties["node.name"] .. " for open clients")
        for client in clients_om:iterate{ type = "client" } do
          if not client["properties"]["wireplumber.daemon"] then
            client:update_permissions{ [node["bound-id"]] = "-" }
          end
        end
        hidden_nodes[node["bound-id"]] = node.properties["object.id"]
      end
    end
  end)
end)

nodes_om:connect("object-removed", function (om, node)
  if filter_nodes[node.properties["object.id"]] then
    log:debug("Freeing filter graph on disconnected node " .. node.properties["node.name"])
    filter_nodes[node.properties["object.id"]] = nil
  end
end)

clients_om:connect("object-added", function (om, client)
  for id, _ in pairs(hidden_nodes) do
    if not client["properties"]["wireplumber.daemon"] then
      client:update_permissions { [id] = "-" }
    end
  end
end)

nodes_om:activate()
clients_om:activate()
