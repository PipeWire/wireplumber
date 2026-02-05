-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Evaluates whether the client is eligible for flatpak access or not.

cutils = require ("common-utils")
log = Log.open_topic ("s-client")

-- The flatpack-manager permission manager
flatpack_manager_pm = PermissionManager ()
flatpack_manager_pm:set_default_permissions (Perm.ALL)

-- The flatpack permission manager
flatpack_pm = PermissionManager ()
flatpack_pm:set_default_permissions (Perm.RX)

SimpleEventHook {
  name = "client/find-flatpak-access",
  before = "client/find-default-access",
  after = "client/find-config-access",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-access" },
    },
  },
  execute = function (event)
    local client = event:get_subject ()
    local app_name = client:get_property ("application.name")
    local access = cutils.get_client_access (client.properties)
    local media_category = client:get_property ("media.category")

    local permission_manager = event:get_data ("permission-manager")
    local effective_access = event:get_data ("effective-access")

    log:debug (client, string.format ("handling client '%s'", app_name))

    -- Check effective access if never set before
    if effective_access == nil then
      if access == "flatpak" and media_category == "Manager" then
        effective_access = "flatpak-manager"
      elseif access == "flatpak" then
        effective_access = "flatpak"
      end

      if effective_access ~= nil then
        log:info (client, string.format (
            "Found %s effective-access for client '%s'",
            effective_access, app_name))
        event:set_data ("effective-access", effective_access)
      end
    end

    -- Check permission manager if never set before
    if permission_manager == nil then
      if access == "flatpak" and media_category == "Manager" then
        log:info (client, string.format (
            "Found flatpak-manager PM for client '%s'", app_name))
        event:set_data ("permission-manager", flatpack_manager_pm)
      elseif access == "flatpak" then
        log:info (client, string.format (
            "Found flatpak PM for client '%s'", app_name))
        event:set_data ("permission-manager", flatpack_pm)
      end
    end
  end
}:register()
