clients_om = ObjectManager {
  Interest {
    type = "client",
    Constraint { "pipewire.access", "=", "flatpak" },
  }
}

clients_om:connect("object-added", function (om, client)
  local id = client["bound-id"]
  Log.info(client, "Granting RX access to client " .. id)
  client:update_permissions { ["any"] = "rx" }
end)

clients_om:activate()
