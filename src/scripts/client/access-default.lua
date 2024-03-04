-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-client")

function getAccess (properties)
  local access = properties["pipewire.access"]
  local client_access = properties["pipewire.client.access"]
  local is_flatpak = properties["pipewire.sec.flatpak"]

  if is_flatpak then
    client_access = "flatpak"
  end

  if client_access == nil then
    return access
  elseif access == "unrestricted" or access == "default" then
    if client_access ~= "unrestricted" then
      return client_access
    end
  end

  return access
end

function getDefaultPermissions (properties)
  local access = properties["access"]
  local media_category = properties["media.category"]

  if access == "flatpak" and media_category == "Manager" then
    return "all", "flatpak-manager"
  elseif access == "flatpak" or access == "restricted" then
    return "rx", access
  elseif access == "default" then
    return "all", access
  end

  return nil, nil
end

function getPermissions (properties)
  local section = Conf.get_section_as_json ("access.rules")
  if section then
    local mprops, matched = JsonUtils.match_rules_update_properties (
        section, properties)
    if (matched > 0 and mprops["default_permissions"]) then
      return mprops["default_permissions"], mprops["access"]
    end
  end

  return nil, nil
end

clients_om = ObjectManager {
  Interest { type = "client" }
}

clients_om:connect("object-added", function (om, client)
  local id = client["bound-id"]
  local properties = client["properties"]
  local access = getAccess (properties)

  properties["access"] = access

  local perms, effective_access = getPermissions (properties)
  if perms == nil then
    perms, effective_access = getDefaultPermissions (properties)
  end
  if effective_access == nil then
    effective_access = access
  end

  if perms ~= nil then
    log:info(client, "Granting permissions to client " .. id .. " (access " ..
      effective_access .. "): " .. perms)
    client:update_permissions { ["any"] = perms }
    client:update_properties { ["pipewire.access.effective"] = effective_access }
  else
    log:debug(client, "No rule for client " .. id .. " (access " .. access .. ")")
  end
end)

clients_om:activate()
