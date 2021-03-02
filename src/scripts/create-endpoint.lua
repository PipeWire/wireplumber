-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

session_items = {
  endpoints = {},
  monitors = {},
}

function addEndpoint (node, session_name, endpoint_type, priority)
  local id = node["bound-id"]
  local media_class = node.properties['media.class']
  local session = nil
  local name = nil

  -- find the session
  session = sessions_om:lookup {
    Constraint { "session.name", "=", session_name }
  }
  if session == nil then
    Log.warning(node, "could not find session");
    return
  end

  -- get the endpoint name
  name = node.properties['node.name'] or "endpoint.node." .. id

  -- create endpoint
  session_items.endpoints[id] = SessionItem ( endpoint_type )

  -- configure endpoint
  if not session_items.endpoints[id]:configure ({
      "node", node,
      "name", name,
      "media-class", media_class,
      "priority", priority,
      }) then
    Log.warning(node, "failed to configure endpoint");
    return
  end

  -- activate endpoint
  session_items.endpoints[id]:activate (function (activated_ep)
    Log.debug(activated_ep, "activated endpoint " .. name);

    -- export endpoint
    activated_ep:export (session, function (exported_ep)
      Log.info(exported_ep, "exported endpoint " .. name);

      -- only use monitor audio sinks
      if media_class == "Audio/Sink" then
        -- create monitor
        local monitor = SessionItem ( "si-monitor-endpoint" )

        -- configure monitor
        if not monitor:configure ({
            "adapter", session_items.endpoints[id]
          }) then
          Log.warning(monitor, "failed to configure monitor " .. name);
          return
        end

        session_items.monitors[id] = monitor

        -- activate monitor
        monitor:activate (function (activated_mon)
          Log.debug(activated_mon, "activated monitor " .. name);

          -- export monitor
          activated_mon:export (session, function (exported_mon)
            Log.info(exported_mon, "exported monitor " .. name);
          end)
        end)
      end
    end)
  end)
end

sessions_om = ObjectManager { Interest { type = "session" } }
nodes_om = ObjectManager { Interest { type = "node" } }

nodes_om:connect("object-added", function (om, node)
  local media_class = node.properties['media.class']

  -- skip nodes without media class
  if media_class == nil then
    return
  end

  if string.find (media_class, "Audio") then
    addEndpoint (node, "audio", "si-adapter", 1)
  else
    addEndpoint (node, "video", "si-simple-node-endpoint", 1)
  end
end)

nodes_om:connect("object-removed", function (om, node)
  local id = node["bound-id"]
  session_items.monitors[id] = nil
  session_items.endpoints[id] = nil
end)

sessions_om:activate()
nodes_om:activate()
