-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Evaluates whether the client is eligible for config access or not.

cutils = require ("common-utils")
log = Log.open_topic ("s-client")

config = {}
config.rules = Conf.get_section_as_json ("access.rules", Json.Array {})
config.permission_managers = Conf.get_section_as_json (
    "access.permission-managers", Json.Array {})

-- Create the config permission managers
permission_managers = {}
config_pm_table = config.permission_managers:parse (2)
for _, pm_info in ipairs (config_pm_table) do
  if pm_info.name == nil then
    log:warning ("Config permission manager does not have a name, ignoring...")
    goto skip_pm
  end

  local config_pm = PermissionManager ()

  -- Set default permissions if defined
  if pm_info.default_permissions ~= nil then
    config_pm:set_default_permissions (pm_info.default_permissions)
  end

  -- Set core permissions if defined
  if pm_info.core_permissions ~= nil then
    config_pm:set_core_permissions (pm_info.core_permissions)
  end

  -- Set rules match if defined
  if pm_info.rules ~= nil then
    config_pm:add_rules_match (Json.Raw (pm_info.rules))
  end

  -- Add it to the table
  permission_managers[pm_info.name] = config_pm
  log:debug ("Added config permission manager: " .. pm_info.name)

  ::skip_pm::
end

SimpleEventHook {
  name = "client/find-config-access",
  before = { "client/find-default-access", "client/apply-access" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-access" },
    },
  },
  execute = function (event)
    local client = event:get_subject ()
    local app_name = client:get_property ("application.name")

    local effective_access = event:get_data ("effective-access")
    local default_permissions = event:get_data ("default-permissions")
    local permission_manager = event:get_data ("permission-manager")

    log:debug (client, string.format ("handling client '%s'", app_name))

    -- We keep backward compatibility to allow matching on 'access' property
    local client_properties = client.properties
    local access = cutils.get_client_access (client_properties)
    client_properties["access"] = access

    -- Update the client propst to get the config access, perms and PM
    local updated_props = JsonUtils.match_rules_update_properties (
        config.rules, client_properties)
    local config_access = updated_props["access"]
    local config_default_perms = updated_props["default_permissions"]
    local config_pm_name = updated_props["permission_manager_name"]

    -- Show warning if both config_default_perms and config_pm_name are defined
    if config_default_perms ~= nil and config_pm_name ~= nil then
      log:warning (client, string.format (
          "Ignoring 'permission_manager_name' property for client '%s'",
          app_name))
    end

    -- Check effective access if never set before
    if effective_access == nil and config_access ~= nil then
      log:info (client, string.format (
          "Found config %s effective-access for client '%s'",
          config_access, app_name))
      event:set_data ("effective-access", config_access)
    end

    -- Check default permissions if never set before
    if default_permissions == nil and config_default_perms ~= nil then
      log:info (client, string.format (
          "Found config '%s' default-permissions for client '%s'",
          config_default_perms, app_name))
      event:set_data ("default-permissions", config_default_perms)
    end

    -- check permission manager if never set before
    if permission_manager == nil and config_default_perms == nil
        and config_pm_name ~= nil then
      local config_pm = permission_managers [config_pm_name]
      if config_pm ~= nil then
        log:info (client, string.format (
            "Found config '%s' PM for client '%s'",
            config_pm_name, app_name))
        event:set_data ("permission-manager", config_pm)
      else
        log:warning (client, string.format (
            "Could not find config '%s' PM for client '%s'",
            config_pm_name, app_name))
      end
    end

  end
}:register()
