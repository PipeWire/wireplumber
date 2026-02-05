-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Evaluates whether the client is eligible for portal access or not.

log = Log.open_topic ("s-client")
pps_plugin = Plugin.find("portal-permissionstore")

MEDIA_ROLE_NONE = 0
MEDIA_ROLE_CAMERA = 1 << 0

function hasPermission (permissions, app_id, lookup)
  if permissions then
    for key, values in pairs(permissions) do
      if key == app_id then
        for _, v in pairs(values) do
          if v == lookup then
            return true
          end
        end
      end
    end
  end
  return false
end

function parseMediaRoles (media_roles_str)
  local media_roles = MEDIA_ROLE_NONE
  for role in media_roles_str:gmatch('[^,%s]+') do
    if role == "Camera" then
      media_roles = media_roles | MEDIA_ROLE_CAMERA
    end
  end
  return media_roles
end

-- The portal permission manager
portal_pm = PermissionManager ()
portal_pm:set_default_permissions (Perm.ALL)

-- Add interest in camera video source nodes
portal_pm:add_interest_match (
  function (_, client, _)
    local client_id = client["bound-id"]
    local str_prop = nil
    local app_id = nil
    local media_roles = nil
    local allowed = false

    -- Give all permissions if portal-permissionstore plugin is not loaded
    if pps_plugin == nil then
      log:info (client, "Portal permission store plugin not loaded")
      return Perm.ALL
    end

    -- Give all permissions to the portal itself
    str_prop = client:get_property ("pipewire.access.portal.is_portal")
    if str_prop == "yes" then
      log:info (client, "client is the portal itself")
      return Perm.ALL
    end

    -- Give all permissions to clients without portal App ID
    str_prop = client:get_property ("pipewire.access.portal.app_id")
    if str_prop == nil then
      log:info (client, "Portal managed client did not set app_id")
      return Perm.ALL
    end

    -- Ignore portal check for non-sandboxed client
    if str_prop == "" then
      log:info (client, "Ignoring portal check for non-sandboxed client")
      return Perm.ALL
    end
    app_id = str_prop

    -- Make sure the client has portal media roles
    str_prop = client:get_property ("pipewire.access.portal.media_roles")
    if str_prop == nil then
      log:info (client, "Portal managed client did not set media_roles")
      return Perm.ALL
    end

    -- Give all permissions to clients without camera role
    media_roles = parseMediaRoles (str_prop)
    if (media_roles & MEDIA_ROLE_CAMERA) == 0 then
      log:info (client, "Ignoring portal check for clients without camera role")
      return Perm.ALL
    end

    -- Check whether the client has allowed access or not
    local permissions = pps_plugin:call("lookup", "devices", "camera");
    allowed = hasPermission (permissions, app_id, "yes")

    -- Return the allowed or not allowed permissions
    log:info (client, "Setting portal camera permissions to " ..
        (allowed and "all" or "none"))
    return allowed and Perm.ALL or Perm.NONE
  end,
  Interest {
    type = "node",
    Constraint { "media.role", "=", "Camera" },
    Constraint { "media.class", "=", "Video/Source" },
  }
)

-- Listen for changes and update permissions when that happens
if pps_plugin ~= nil then
  pps_plugin:connect("changed", function (p, table, id, deleted, permissions)
    if table == "devices" or id == "camera" then
      portal_pm:update_permissions ()
    end
  end)
end

SimpleEventHook {
  name = "client/find-portal-access",
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

    local permission_manager = event:get_data ("permission-manager")

    log:debug (client, string.format ("handling client '%s'", app_name))

    -- Bypass the hook if the permission manager is already picked up
    if permission_manager ~= nil then
      return
    end

    local access = client:get_property ("pipewire.access")
    if access == "portal" then
      log:info (client, string.format (
          "Found portal PM for client '%s'", app_name))
      event:set_data ("permission-manager", portal_pm)
    end
  end
}:register()
