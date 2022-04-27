-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

function rulesGetDefaultPermissions(properties)
  local matched, mprops = Settings.apply_rule ("access", properties)

  if (matched and mprops["default_permissions"]) then
    return mprops["default_permissions"]
  end
end

clients_om = ObjectManager {
  Interest { type = "client" }
}

clients_om:connect("object-added", function (om, client)
  local id = client["bound-id"]
  local properties = client["properties"]

  local perms = rulesGetDefaultPermissions(properties)

  if perms then
    Log.info(client, "Granting permissions to client " .. id .. ": " .. perms)
    client:update_permissions { ["any"] = perms }
  end
end)

clients_om:activate()
