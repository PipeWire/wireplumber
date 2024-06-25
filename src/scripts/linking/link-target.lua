-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Links a session item to the target that has been previously selected.
-- This is meant to be the last hook in the select-target chain.

lutils = require ("linking-utils")
cutils = require ("common-utils")
log = Log.open_topic ("s-linking")

AsyncEventHook {
  name = "linking/link-target",
  after = "linking/prepare-link",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local source, om, si, si_props, si_flags, target =
            lutils:unwrap_select_target_event (event)

        if not target then
          -- bypass the hook, nothing to link to.
          transition:advance ()
          return
        end

        local target_props = target.properties
        local out_item = nil
        local in_item = nil
        local si_link = nil
        local passthrough = si_flags.can_passthrough

        log:info (si, string.format ("handling item %d: %s (%s)", si.id,
            tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

        local exclusive = cutils.parseBool (si_props ["node.exclusive"])

        -- break rescan if tried more than 5 times with same target
        if si_flags.failed_peer_id ~= nil and
            si_flags.failed_peer_id == target.id and
            si_flags.failed_count ~= nil and
            si_flags.failed_count > 5 then
          transition:return_error ("tried to link on last rescan, not retrying "
              .. tostring (si_link))
          return
        end

        if si_props["item.node.direction"] == "output" then
          -- playback
          out_item = si
          in_item = target
        else
          -- capture
          in_item = si
          out_item = target
        end

        local is_media_role_link = target_props["device.intended-roles"] ~= nil

        log:info (si,
          string.format ("link %s <-> %s passthrough:%s, exclusive:%s, media role link:%s",
            tostring (si_props ["node.name"]),
            tostring (target_props ["node.name"]),
            tostring (passthrough),
            tostring (exclusive),
            tostring (is_media_role_link)))

        -- create and configure link
        si_link = SessionItem ("si-standard-link")
        if not si_link:configure {
          ["out.item"] = out_item,
          ["in.item"] = in_item,
          ["passthrough"] = passthrough,
          ["exclusive"] = exclusive,
          ["out.item.port.context"] = "output",
          ["in.item.port.context"] = "input",
          ["media.role"] = si_props["media.role"],
          ["target.media.class"] = target_props["media.class"],
          ["policy.role-based.priority"] = target_props["policy.role-based.priority"],
          ["policy.role-based.action.same-priority"] = target_props["policy.role-based.action.same-priority"],
          ["policy.role-based.action.lower-priority"] = target_props["policy.role-based.action.lower-priority"],
          ["is.media.role.link"] = is_media_role_link,
          ["main.item.id"] = si.id,
          ["target.item.id"] = target.id,
        } then
          transition:return_error ("failed to configure si-standard-link "
            .. tostring (si_link))
          return
        end

        local ids = {si.id, target.id}
        si_link:connect("link-error", function (_, error_msg)
          for _, id in ipairs (ids) do
            local si = om:lookup {
              Constraint { "id", "=", id, type = "gobject" },
            }
            if si then
              local node = si:get_associated_proxy ("node")
              lutils.sendClientError(event, node, -32, error_msg)
            end
          end
        end)

        -- register
        si_flags.was_handled = true
        si_flags.peer_id = target.id
        si_flags.failed_peer_id = target.id
        if si_flags.failed_count ~= nil then
          si_flags.failed_count = si_flags.failed_count + 1
        else
          si_flags.failed_count = 1
        end
        si_link:register ()

        log:debug (si_link, "registered link between "
            .. tostring (si) .. " and " .. tostring (target))

        -- only activate non media role links because their activation is
        -- handled by rescan-media-role-links.lua
        if not is_media_role_link then
          si_link:activate (Feature.SessionItem.ACTIVE, function (l, e)
            if e then
              transition:return_error (tostring (l) .. " link failed: "
                  .. tostring (e))
              if si_flags ~= nil then
                si_flags.peer_id = nil
              end
              l:remove ()
            else
              si_flags.failed_peer_id = nil
              if si_flags.peer_id == nil then
                si_flags.peer_id = target.id
              end
              si_flags.failed_count = 0

              log:debug (l, "activated link between "
                  .. tostring (si) .. " and " .. tostring (target))

              transition:advance ()
            end
          end)
        else
          lutils.updatePriorityMediaRoleLink(si_link)
          transition:advance ()
        end
      end,
    },
  },
}:register ()
