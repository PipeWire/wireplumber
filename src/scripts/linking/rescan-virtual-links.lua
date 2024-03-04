-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Julian Bouzas <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

lutils = require ("linking-utils")
log = Log.open_topic ("s-linking")

defaults = {}
defaults.duck_level = 0.3

config = {}
config.duck_level = default.duck_level -- FIXME
config.roles = Conf.get_section_as_object ("virtual-item-roles")

-- enable ducking if mixer-api is loaded
mixer_api = Plugin.find("mixer-api")

function findRole (role)
  if role and not config.roles[role] then
    for r, p in pairs(config.roles) do
      if type(p.alias) == "table" then
        for i = 1, #(p.alias), 1 do
          if role == p.alias[i] then
            return r
          end
        end
      end
    end
  end
  return role
end

function getRolePriority (role)
  local r = role and config.roles[role] or nil
  return r and r.priority or 0
end

function getAction (dominant_role, other_role)
  -- default to "mix" if the role is not configured
  if not dominant_role or not config.roles[dominant_role] then
    return "mix"
  end

  local role_config = config.roles[dominant_role]
  return role_config["action." .. other_role]
      or role_config["action.default"]
      or "mix"
end

function restoreVolume (om, role, media_class)
  if not mixer_api then return end

  local si_v = om:lookup {
    type = "SiLinkable",
    Constraint { "item.factory.name", "=", "si-audio-virtual", type = "pw-global" },
    Constraint { "media.role", "=", role, type = "pw" },
    Constraint { "media.class", "=", media_class, type = "pw" },
  }

  if si_v then
    local n = si_v:get_associated_proxy ("node")
    if n then
      log:debug(si_v, "restore role " .. role)
      mixer_api:call("set-volume", n["bound-id"], {
        monitorVolume = 1.0,
      })
    end
  end
end

function duckVolume (om, role, media_class)
  if not mixer_api then return end

  local si_v = om:lookup {
    type = "SiLinkable",
    Constraint { "item.factory.name", "=", "si-audio-virtual", type = "pw-global" },
    Constraint { "media.role", "=", role, type = "pw" },
    Constraint { "media.class", "=", media_class, type = "pw" },
  }

  if si_v then
    local n = si_v:get_associated_proxy ("node")
    if n then
      log:debug(si_v, "duck role " .. role)
      mixer_api:call("set-volume", n["bound-id"], {
        monitorVolume = config.duck_level,
      })
    end
  end
end

function getSuspendPlaybackFromMetadata (om)
  local suspend = false
  local metadata = om:lookup {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
  if metadata then
    local value = metadata:find(0, "suspend.playback")
    if value then
      suspend = value == "1" and true or false
    end
  end
  return suspend
end

AsyncEventHook {
  name = "linking/rescan-virtual-links",
  interests = {
    EventInterest {
      -- on virtual client link added and removed
      Constraint { "event.type", "c", "session-item-added", "session-item-removed" },
      Constraint { "event.session-item.interface", "=", "link" },
      Constraint { "is.virtual.client.link", "=", true },
    },
    EventInterest {
      -- on default metadata suspend.playback changed
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "default" },
      Constraint { "event.subject.key", "=", "suspend.playback" },
    }
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local source = event:get_source ()
        local om = source:call ("get-object-manager", "session-item")
        local metadata_om = source:call ("get-object-manager", "metadata")
        local suspend = getSuspendPlaybackFromMetadata (metadata_om)
        local pending_activations = 0
        local links = {
          ["Audio/Source"] = {},
          ["Audio/Sink"] = {},
          ["Video/Source"] = {},
        }

        -- gather info about links
        log:info ("Rescanning virtual si-standard-link links...")
        for silink in om:iterate {
            type = "SiLink",
            Constraint { "is.virtual.client.link", "=", true },
          } do

          -- deactivate all links if suspend playback metadata is present
          if suspend then
            silink:deactivate (Feature.SessionItem.ACTIVE)
          end

          local props = silink.properties
          local role = props["media.role"]
          local target_class = props["target.media.class"]
          local plugged = props["item.plugged.usec"]
          local active = ((silink:get_active_features() & Feature.SessionItem.ACTIVE) ~= 0)
          if links[target_class] then
            table.insert(links[target_class], {
              silink = silink,
              role = findRole (role),
              active = active,
              priority = getRolePriority (role),
              plugged = plugged and tonumber(plugged) or 0
            })
          end
        end

        local function onVirtualLinkActivated (l, e)
          local si_id = tonumber (l.properties ["main.item.id"])
          local target_id = tonumber (l.properties ["target.item.id"])
          local si_flags = lutils:get_flags (si_id)

          if e then
            log:warning (l, "failed to activate virtual si-standard-link: " .. e)
            if si_flags ~= nil then
              si_flags.peer_id = nil
            end
            l:remove ()
          else
            log:info (l, "virtual si-standard-link activated successfully")
            si_flags.failed_peer_id = nil
            if si_flags.peer_id == nil then
              si_flags.peer_id = target_id
            end
            si_flags.failed_count = 0
          end

          -- advance only when all pending activations are completed
          pending_activations = pending_activations - 1
          if pending_activations <= 0 then
            log:info ("All virtual si-standard-links activated")
            transition:advance ()
          end
        end

        local function compareLinks(l1, l2)
          return (l1.priority > l2.priority) or
              ((l1.priority == l2.priority) and (l1.plugged > l2.plugged))
        end

        for media_class, v in pairs(links) do
          -- sort on priority and stream creation time
          table.sort(v, compareLinks)

          -- apply actions
          local first_link = v[1]
          if first_link then
            for i = 2, #v, 1 do
              local action = getAction(first_link.role, v[i].role)
              if action == "cork" then
                if v[i].active then
                  v[i].silink:deactivate(Feature.SessionItem.ACTIVE)
                end
              elseif action == "mix" then
                if not v[i].active and not suspend then
                  pending_activations = pending_activations + 1
                  v[i].silink:activate (Feature.SessionItem.ACTIVE,
                      onVirtualLinkActivated)
                end
                restoreVolume(om, v[i].role, media_class)
              elseif action == "duck" then
                if not v[i].active and not suspend then
                  pending_activations = pending_activations + 1
                  v[i].silink:activate (Feature.SessionItem.ACTIVE,
                      onVirtualLinkActivated)
                end
                duckVolume (om, v[i].role, media_class)
              else
                log:warning("Unknown action: " .. action)
              end
            end

            if not first_link.active and not suspend then
              pending_activations = pending_activations + 1
              first_link.silink:activate(Feature.SessionItem.ACTIVE,
                 onVirtualLinkActivated)
            end
            restoreVolume (om, first_link.role, media_class)
          end
        end

        -- just advance transition if no pending activations are needed
        if pending_activations <= 0 then
          log:info ("All virtual si-standard-links rescanned")
          transition:advance ()
        end
      end,
    },
  },
}:register ()
