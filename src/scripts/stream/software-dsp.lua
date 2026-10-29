-- WirePlumber
--
-- Copyright © 2022-2023 The WirePlumber project contributors
--    @author Dmitry Sharshakov <d3dx12.xx@gmail.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic("s-stream")

config = {}
config.rules = Conf.get_section_as_json("stream.software-dsp.rules", Json.Array{})

-- TODO: port from Obj Manager to Hooks
clients_om = ObjectManager {
  Interest { type = "client" }
}

filter_streams = {}
hidden_nodes = {}

SimpleEventHook {
  name = "stream/dsp/create-dsp-node",
  interests = {
    EventInterest {
      Constraint  { "event.type", "=", "stream-added" },
    },
  },
  execute = function(event)
    local stream = event:get_subject()
    local stream_props = stream:get_properties()
    local stream_name = stream_props["node.name"]
    JsonUtils.match_rules (config.rules, stream_props, function (action, value)
      if action == "create-filter" then
        local props = value:parse (1)
        log:debug("DSP rule found for " .. stream_name)

        if props["filter-graph"] then
          log:debug("Loading filter graph for " .. stream_name)
          filter_streams[stream.id] = LocalModule("libpipewire-module-filter-chain", props["filter-graph"], {})
        elseif props["filter-path"] then
          log:debug("Loading filter graph for " .. stream_name .. " from disk")
          local conf = Conf(props["filter-path"], {
            ["as-section"] = "stream.software-dsp.graph",
            ["no-fragments"] = true
          })
          local err = conf:open()
          if not err then
            local args = conf:get_section_as_json("stream.software-dsp.graph"):to_string()
            filter_streams[stream.id] = LocalModule("libpipewire-module-filter-chain", args, {})
          else
            log:warning("Unable to load filter graph for " .. stream_name)
          end
        end

        -- Permissions apply to PipeWire objects, so the stream node is hidden
        if props["hide-parent"] then
          local bound_id = stream:get_node_bound_id()
          log:debug("Setting permissions to '-' on " .. stream_name .. " for open clients")
          for client in clients_om:iterate{ type = "client" } do
            if not client["properties"]["wireplumber.daemon"] then
              client:update_permissions { [bound_id] = Perm.NONE }
            end
          end
          hidden_nodes[stream.id] = bound_id
        end
      end
    end)
  end
}:register()

SimpleEventHook {
  name = "stream/dsp/free-dsp-node",
  interests = {
    EventInterest {
      Constraint  { "event.type", "=", "stream-removed" },
    },
  },
  execute = function(event)
    local stream = event:get_subject()
    if filter_streams[stream.id] then
      log:debug("Freeing filter on stream " .. stream.id)
      filter_streams[stream.id] = nil
    end
    hidden_nodes[stream.id] = nil
  end
}:register()

clients_om:connect("object-added", function (om, client)
  for _, id in pairs(hidden_nodes) do
    if not client["properties"]["wireplumber.daemon"] then
      client:update_permissions { [id] = Perm.NONE }
    end
  end
end)

clients_om:activate()
