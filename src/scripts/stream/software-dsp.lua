-- WirePlumber
--
-- Copyright © 2022-2023 The WirePlumber project contributors
--    @author Dmitry Sharshakov <d3dx12.xx@gmail.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic("s-stream")

config = {}
config.rules = Conf.get_section_as_json("stream.software-dsp.rules", Json.Array{})

filter_streams = {}
hidden_nodes = {}

AsyncEventHook {
  name = "stream/dsp/create-dsp-node",
  interests = {
    EventInterest {
      Constraint  { "event.type", "=", "stream-added" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local source = event:get_source ()
        local stream = event:get_subject ()
        local stream_props = stream:get_properties ()
        local stream_name = stream_props["node.name"]
        local filter_args = nil

        JsonUtils.match_rules (config.rules, stream_props, function (action, value)
          if action == "create-filter" then
            local props = value:parse (1)
            log:debug("DSP rule found for " .. stream_name)

            if props["filter-graph"] then
              log:debug("Loading filter graph for " .. stream_name)
              filter_args = props["filter-graph"]
            elseif props["filter-path"] then
              log:debug("Loading filter graph for " .. stream_name .. " from disk")
              local conf = Conf(props["filter-path"], {
                ["as-section"] = "stream.software-dsp.graph",
                ["no-fragments"] = true
              })
              local err = conf:open()
              if not err then
                filter_args = conf:get_section_as_json("stream.software-dsp.graph"):to_string()
              else
                log:warning("Unable to load filter graph for " .. stream_name)
              end
            end

            -- Permissions apply to PipeWire objects, so the stream node is hidden
            if props["hide-parent"] then
              local bound_id = stream:get_node_bound_id()
              log:debug("Setting permissions to '-' on " .. stream_name .. " for open clients")
              local clients_om = source:call("get-object-manager", "client")
              for client in clients_om:iterate() do
                if not client["properties"]["wireplumber.daemon"] then
                  client:update_permissions { [bound_id] = Perm.NONE }
                end
              end
              hidden_nodes[stream.id] = bound_id
            end
          end
        end)

        if filter_args == nil then
          transition:advance ()
          return
        end

        local m = LocalModule("libpipewire-module-filter-chain", filter_args, {})
        filter_streams[stream.id] = m
        m:activate (Feature.LocalModule.LOADED, function (_, e)
          if e then
            if filter_streams[stream.id] == m then
              filter_streams[stream.id] = nil
            end
            transition:return_error ("failed to load filter graph for " ..
                stream_name .. ": " .. tostring (e))
          else
            transition:advance ()
          end
        end)
      end
    },
  },
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

SimpleEventHook {
  name = "stream/dsp/hide-parent-from-new-client",
  -- Hide the nodes before select-access grants the client its permissions
  before = "client/select-access-trigger",
  interests = {
    EventInterest {
      Constraint  { "event.type", "=", "client-added" },
    },
  },
  execute = function(event)
    local client = event:get_subject()
    if client["properties"]["wireplumber.daemon"] then
      return
    end
    for _, id in pairs(hidden_nodes) do
      client:update_permissions { [id] = Perm.NONE }
    end
  end
}:register()
