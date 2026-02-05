-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Applies effective access and permissions to clients.

log = Log.open_topic ("s-client")

AsyncEventHook {
  name = "client/apply-access",
  after = { "client/find-config-access", "client/find-default-access" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-access" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local client = event:get_subject ()
        local app_name = client:get_property ("application.name")

        local effective_access = event:get_data ("effective-access")
        local default_permissions = event:get_data ("default-permissions")
        local permission_manager = event:get_data ("permission-manager")

        log:debug (client, string.format ("handling client '%s'", app_name))

        -- Set effective access if any
        if effective_access ~= nil then
          client:update_properties {
            ["pipewire.access.effective"] = effective_access
          }
          log:info (client, string.format (
              "Updated effective access on client '%s' to '%s'", app_name,
              effective_access))
        end

        -- Set defaut permissions if any, otherwise check permission manager
        if default_permissions ~= nil then
          client:update_permissions { ["any"] = default_permissions }
          log:info (client, string.format (
              "Updated default permissions on client '%s' to '%s'", app_name,
              default_permissions))
          transition:advance ()
        elseif permission_manager ~= nil then
          -- Make sure the permission manager is activated
          permission_manager:activate (Features.ALL, function (pm, e)
            if e then
              transition:return_error (string.format (
                  "failed to activate permission manager for client '%s': %s",
                  app_name, e))
              return
            end

            -- Attach permission manager to client so permissions are applied
            client:attach_permission_manager (permission_manager)
            log:info (client, string.format (
                "Attached permission manager to client '%s'", app_name))
            transition:advance ()
          end)
        else
          log:info (client, string.format (
                "Handled client '%s' without updating permissions", app_name))
          transition:advance ()
        end
      end,
    },
  },
}:register()
