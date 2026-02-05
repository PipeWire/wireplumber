-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Evaluates whether the client is eligible for snap access or not.

log = Log.open_topic ("s-client")

-- The snap permission manager
snap_pm = PermissionManager ()
snap_pm:set_default_permissions (Perm.ALL)

-- Always remove permissions for all non-snap clients
snap_pm:add_interest_match_simple (Perm.NONE,
  Interest {
    type = "client",
    Constraint { "pipewire.snap.id", "-", type = "pw"},
  }
)

-- Always remove permissions for all snap clients with different snap_id
snap_pm:add_interest_match (
  function (_, client, object)
    client_snap_id = client:get_property ("pipewire.snap.id")
    object_snap_id = object:get_property ("pipewire.snap.id")
    return client_snap_id == object_snap_id and Perm.ALL or Perm.NONE
  end,
  Interest {
    type = "client",
    Constraint { "pipewire.snap.id", "+", type = "pw"},
  }
)

-- Check playback node permissions
snap_pm:add_interest_match (
  function (_, client, _)
    local allowed = client.properties:get_boolean ("pipewire.snap.audio.playback")
    return allowed and Perm.ALL or Perm.NONE
  end,
  Interest {
    type = "node",
    Constraint { "media.class", "=", "Audio/Sink"}
  }
)

-- Check record node permissions
snap_pm:add_interest_match (
  function (_, client, _)
    local allowed = client.properties:get_boolean ("pipewire.snap.audio.record")
    return allowed and Perm.ALL or Perm.NONE
  end,
  Interest {
    type = "node",
    Constraint { "media.class", "=", "Audio/Source"}
  }
)

SimpleEventHook {
  name = "client/find-snap-access",
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

    local snap_id = client:get_property ("pipewire.snap.id")
    if snap_id ~= nil then
      log:info (client, string.format (
          "Found snap PM for client '%s'", app_name))
      event:set_data ("permission-manager", snap_pm)
    end
  end
}:register()
