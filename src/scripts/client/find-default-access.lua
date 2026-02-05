-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Evaluates whether the client is eligible for default access or not.

cutils = require ("common-utils")
log = Log.open_topic ("s-client")

-- The default permission manager
default_pm = PermissionManager ()
default_pm:set_default_permissions (Perm.ALL)

-- The default-restricted permission manager
default_restricted_pm = PermissionManager ()
default_restricted_pm:set_default_permissions (Perm.RX)

SimpleEventHook {
  name = "client/find-default-access",
  before = "client/apply-access",
  after = "client/find-config-access",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-access" },
    },
  },
  execute = function (event)
    local client = event:get_subject ()
    local app_name = client:get_property ("application.name")

    local permission_manager = event:get_data ("permission-manager")
    local effective_access = event:get_data ("effective-access")

    log:debug (client, string.format ("handling client '%s'", app_name))

    -- Check effective access if never set before
    if effective_access == nil then
      local access = cutils.get_client_access (client.properties)
      if access ~= nil then
        log:info (client, string.format (
            "Found default %s effective-access for client '%s'", access, app_name))
        event:set_data ("effective-access", access)
      end
    end

    -- Check permission manager if never set before
    if permission_manager == nil then
      if access == "restricted" then
        log:info (client, string.format (
          "Found default-restricted PM for client '%s'", app_name))
        event:set_data ("permission-manager", default_restricted_pm)
      else
        log:info (client, string.format (
          "Found default PM for client '%s'", app_name))
        event:set_data ("permission-manager", default_pm)
      end
    end
  end
}:register()
