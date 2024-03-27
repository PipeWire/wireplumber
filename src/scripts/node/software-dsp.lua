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
clients_om = ObjectManager {
  Interest { type = "client" }
}

filter_nodes = {}
hidden_nodes = {}

SimpleEventHook {
  name = "node/dsp/create-dsp-node",
  interests = {
    EventInterest {
      Constraint  { "event.type", "=", "node-added" },
    },
  },
  execute = function(event)
    local node = event:get_subject()
    JsonUtils.match_rules (config.rules, node.properties, function (action, value)
      if action == "create-filter" then
        local props = value:parse (1)
        log:debug("DSP rule found for " .. node.properties["node.name"])

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
  end
}:register()

SimpleEventHook {
  name = "node/dsp/free-dsp-node",
  interests = {
    EventInterest {
      Constraint  { "event.type", "=", "node-removed" },
    },
  },
  execute = function(event)
    local node = event:get_subject()
    if filter_nodes[node.properties["object.id"]] then
      log:debug("Freeing filter graph on disconnected node " .. node.properties["node.name"])
      filter_nodes[node.properties["object.id"]] = nil
    end
  end
}:register()

clients_om:connect("object-added", function (om, client)
  for id, _ in pairs(hidden_nodes) do
    if not client["properties"]["wireplumber.daemon"] then
      client:update_permissions { [id] = "-" }
    end
  end
end)

clients_om:activate()
