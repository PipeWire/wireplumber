-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Evaluates whether the client is eligible for portal access or not.

log = Log.open_topic ("s-client")
pps_plugin = Plugin.find("portal-permissionstore")
cached_camera_permissions = nil
camera_permissions_loaded = false

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

function getCameraPermissions ()
  if not camera_permissions_loaded then
    cached_camera_permissions = pps_plugin:call("lookup", "devices", "camera")
    camera_permissions_loaded = true
  end

  return cached_camera_permissions
end

-- Advertise portal client gate support to the PW daemon.
-- This is set once on our own client at script load time
-- so PW's core_hello can detect that a capable session manager is
-- connected, regardless of which portal client is being processed.
Core.update_properties {
  ["pipewire.access.portal.gate-supported"] = "true"
}

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
    local permissions = getCameraPermissions ()
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

-- Handle portal client gating/ungating.
-- The PW daemon gates portal clients on fd handoff
-- (when an app connects via the stolen fd) by removing
-- PW_PERM_R from PW_ID_CORE and setting the property
-- pipewire.access.portal.gated to true. This makes
-- the client busy so the daemon stops reading from the
-- socket until permissions are set up properly.
--
-- two timing scenarios handled:
-- 1) gate is already set so ungate immediately.
-- 2) gate is set later (app connects after PermissionManager attached)
--    watch for the property change and ungate then.
portal_pm:connect ("client-properties-changed", function (pm, c)
  local app_name = c:get_property ("application.name")
  local gated = c:get_property ("pipewire.access.portal.gated")
  if gated == "true" then
    c:update_permissions { [0] = "rwx" }
    log:info (c, string.format (
        "Ungated portal client '%s' (via property change)",
        app_name))
  end
end)

-- Listen for changes and update permissions when that happens
if pps_plugin ~= nil then
  pps_plugin:connect("changed", function (p, table, id, deleted, permissions)
    if table == "devices" or id == "camera" then
      cached_camera_permissions = permissions
      camera_permissions_loaded = true
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
