-- Manage snap audio permissions
--
-- Copyright © 2023 Canonical Ltd.
--    @author Sergio Costas Rodriguez <sergio.costas@canonical.com>
--
-- SPDX-License-Identifier: MIT

function removeClientPermissionsForOtherClients (client)
  -- Remove access to any other clients, but allow all the process of the
  -- same snap to access their elements
  local client_id = client.properties["pipewire.snap.id"]
  for snap_client in clients_snap:iterate() do
    local snap_client_id = snap_client.properties["pipewire.snap.id"]
    if snap_client_id ~= client_id then
      client:update_permissions { [snap_client["bound-id"]] = "-" }
    end
  end
  for no_snap_client in clients_no_snap:iterate() do
    client:update_permissions { [no_snap_client["bound-id"]] = "-" }
  end
end

function updateClientPermissions (client)
  -- Remove access to Audio/Sources and Audio/Sinks based on snap permissions
  for node in nodes_om:iterate() do
    local node_id = node["bound-id"]
    local property = "pipewire.snap.audio.playback"

    if node.properties["media.class"] == "Audio/Source" then
      property = "pipewire.snap.audio.record"
    end

    if client.properties[property] ~= "true" then
      client:update_permissions { [node_id] = "-" }
    end
  end
end

clients_snap = ObjectManager {
  Interest {
    type = "client",
    Constraint { "pipewire.snap.id", "+", type = "pw"},
  }
}

clients_no_snap = ObjectManager {
  Interest {
    type = "client",
    Constraint { "pipewire.snap.id", "-", type = "pw"},
  }
}

nodes_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "media.class", "matches", "Audio/*"}
  }
}

clients_snap:connect("object-added", function (om, client)
  -- If a new snap client is added, adjust its permissions
  updateClientPermissions (client)
  removeClientPermissionsForOtherClients (client)
end)

clients_no_snap:connect("object-added", function (om, client)
  -- If a new, non-snap client is added,
  -- remove access to it from other snaps
  client_id = client["bound-id"]
  for snap_client in clients_snap:iterate() do
    if client.properties["pipewire.snap.id"] ~= nil then
      snap_client:update_permissions { [client_id] = "-" }
    end
  end
end)

nodes_om:connect("object-added", function (om, node)
  -- If a new Audio/Sink or Audio/Source node is added,
  -- adjust the permissions in the snap clients
  for client in clients_snap:iterate() do
    updateClientPermissions (client)
  end
end)

clients_snap:activate()
clients_no_snap:activate()
nodes_om:activate()