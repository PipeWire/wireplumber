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
  if not session_items.endpoints[id]:configure {
      ["node"] = node,
      ["name"] = name,
      ["media-class"] = media_class,
      ["priority"] = priority,
      ["session"] = session,
  } then
    Log.warning(node, "failed to configure endpoint");
    return
  end

  -- activate and export endpoint
  session_items.endpoints[id]:activate (Feature.Object.ALL, function (activated_ep)
    Log.info(activated_ep, "activated and exported endpoint " .. name);

    -- only use monitor audio sinks
    if media_class == "Audio/Sink" then
      -- create monitor
      local monitor = SessionItem ( "si-monitor" )

      -- configure monitor
      if not monitor:configure {
          ["endpoint"] = session_items.endpoints[id],
          ["name"] = "Monitor." .. name,
          ["session"] = session,
      } then
        Log.warning(monitor, "failed to configure monitor " .. name);
        return
      end

      session_items.monitors[id] = monitor

      -- activate and export monitor
      monitor:activate (Feature.Object.ALL, function (activated_mon)
        Log.info(activated_mon, "activated and exported monitor " .. name);
      end)
    end
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
    addEndpoint (node, "audio", "si-audio-adapter", 1)
  else
    addEndpoint (node, "video", "si-node", 1)
  end
end)

nodes_om:connect("object-removed", function (om, node)
  local id = node["bound-id"]
  session_items.monitors[id] = nil
  session_items.endpoints[id] = nil
end)

sessions_om:activate()
nodes_om:activate()
