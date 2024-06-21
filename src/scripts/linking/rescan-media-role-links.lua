-- WirePlumber
--
-- Copyright © 2024 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

lutils = require("linking-utils")
cutils = require("common-utils")
log = Log.open_topic("s-linking")

config = {}
config.duck_level = Settings.get_float("linking.duck-level")

function restoreVolume (om, link)
  setVolume(om, link, 1.0)
end

function duckVolume (om, link)
  setVolume(om, link, config.duck_level)
end

function setVolume (om, link, level)
  local lprops = link.properties
  local media_role_si_id = nil
  local dir = lprops ["item.node.direction"]

  if dir == "output" then
    media_role_si_id = lprops ["out.item.id"]
  else
    media_role_si_id = lprops ["in.item.id"]
  end

  local media_role_lnkbl = om:lookup {
    type = "SiLinkable",
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "id", "=", media_role_si_id, type = "gobject" },
  }

  -- apply volume control on the stream node of the loopback module, instead of
  -- the sink/source node as it simplyfies the volume ducking and
  -- restoration.
  local media_role_other_lnkbl = om:lookup {
    type = "SiLinkable",
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "node.link-group", "=", media_role_lnkbl.properties ["node.link-group"] },
    Constraint { "id", "!", media_role_lnkbl.id, type = "gobject" },
  }

  if media_role_other_lnkbl then
    local n = media_role_other_lnkbl:get_associated_proxy("node")
    if n then
      log:info(string.format(".. %s volume of media role node \"%s(%d)\" to %f",
        level < 1.0 and "duck" or "restore", n.properties ["node.name"],
        n ["bound-id"], level))

      local props = {
        "Spa:Pod:Object:Param:Props",
        "Props",
        volume = level,
      }

      local param = Pod.Object(props)
      n:set_param("Props", param)
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
  name = "linking/rescan-media-role-links",
  interests = {
    EventInterest {
      -- on media client link added and removed
      Constraint { "event.type", "c", "session-item-added", "session-item-removed" },
      Constraint { "event.session-item.interface", "=", "link" },
      Constraint { "is.media.role.link", "=", true },
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
      execute = function(event, transition)
        local source, om, _, si_props, _, _ =
            lutils:unwrap_select_target_event(event)

        local metadata_om = source:call("get-object-manager", "metadata")
        local suspend = getSuspendPlaybackFromMetadata(metadata_om)
        local pending_activations = 0
        local mc = si_props ["target.media.class"]
        local pmrl_active = nil
        pmrl = lutils.getPriorityMediaRoleLink(mc)

        log:debug("Rescanning media role links...")

        local function onMediaRoleLinkActivated (l, e)
          local si_id = tonumber(l.properties ["main.item.id"])
          local target_id = tonumber(l.properties ["target.item.id"])
          local si_flags = lutils:get_flags(si_id)

          if e then
            log:warning(l, "failed to activate media role link: " .. e)
            if si_flags ~= nil then
              si_flags.peer_id = nil
            end
            l:remove()
          else
            log:info(l, "media role link activated successfully")
            si_flags.failed_peer_id = nil
            if si_flags.peer_id == nil then
              si_flags.peer_id = target_id
            end
            si_flags.failed_count = 0
          end

          -- advance only when all pending activations are completed
          pending_activations = pending_activations - 1
          if pending_activations <= 0 then
            log:info("All media role links activated")
            transition:advance()
          end
        end

        for link in om:iterate {
          type = "SiLink",
          Constraint { "is.media.role.link", "=", true },
          Constraint { "target.media.class", "=", mc },
        } do
          -- deactivate all links if suspend playback metadata is present
          if suspend then
            link:deactivate(Feature.SessionItem.ACTIVE)
          end

          local active = ((link:get_active_features() & Feature.SessionItem.ACTIVE) ~= 0)

          log:debug(string.format(" .. looking at link(%d) active %s pmrl %s", link.id, tostring(active),
            tostring(link == pmrl)))

          if link == pmrl then
            pmrl_active = active
            restoreVolume(om, pmrl)
            goto continue
          end

          local action = lutils.getAction(pmrl, link)

          log:debug(string.format(" .. apply action(%s) on link(%d)", action, link.id, tostring(active)))

          if action == "cork" then
            if active then
              link:deactivate(Feature.SessionItem.ACTIVE)
            end
          elseif action == "mix" then
            if not active and not suspend then
              pending_activations = pending_activations + 1
              link:activate(Feature.SessionItem.ACTIVE, onMediaRoleLinkActivated)
            end
            restoreVolume(om, link)
          elseif action == "duck" then
            if not active and not suspend then
              pending_activations = pending_activations + 1
              link:activate(Feature.SessionItem.ACTIVE, onMediaRoleLinkActivated)
            end
            duckVolume(om, link)
          else
            log:warning("Unknown action: " .. action)
          end

          ::continue::
        end

        if pmrl and not pmrl_active then
          pending_activations = pending_activations + 1
          pmrl:activate(Feature.SessionItem.ACTIVE, onMediaRoleLinkActivated)
          restoreVolume(om, pmrl)
        end

        -- just advance transition if no pending activations are needed
        if pending_activations <= 0 then
          log:debug("All media role links rescanned")
          transition:advance()
        end
      end,
    },
  },
}:register()
