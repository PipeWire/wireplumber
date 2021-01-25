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
  local name = node.properties['node.name']
  local media_class = node.properties['media.class']
  local session = nil

  -- find the session
  session = sessions_om:lookup(Interest { type = "session",
    Constraint { "session.name", "=", session_name, type = "pw-global" }
  })
  if session == nil then
    Log.warning(node, "could not find session");
    return
  end

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
    Log.debug(node, "activated endpoint " .. name);

    -- export endpoint
    activated_ep:export (session, function (exported_ep)
      Log.info(node, "exported endpoint " .. name);

      -- only use monitor for input endpoints
      if string.find (media_class, "Input") or string.find (media_class, "Sink") then
        -- create monitor
        session_items.monitors[id] = SessionItem ( "si-monitor-endpoint" )

        -- configure monitor
	if not session_items.monitors[id]:configure ({
	    "adapter", session_items.endpoints[id]
	  }) then
	  Log.warning(node, "failed to configure monitor " .. name);
	end

	-- activate monitor
	session_items.monitors[id]:activate (function (activated_mon)
	  Log.debug(node, "activated monitor " .. name);

	  -- export monitor
	  activated_mon:export (session, function (exported_mon)
	    Log.info(node, "exported monitor " .. name);
	  end)
	end)
      end
    end)
  end)
end

function removeEndpoint (node)
  local id = node["bound-id"]
  session_items.monitors[id] = nil
  session_items.endpoints[id] = nil
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
  local media_class = node.properties['media.class']

  -- skip nodes without media class
  if media_class == nil then
    return
  end

  removeEndpoint (node)
end)

sessions_om:activate()
nodes_om:activate()
