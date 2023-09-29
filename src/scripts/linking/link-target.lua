-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Links a session item to the target that has been previously selected.
-- This is meant to be the last hook in the select-target chain.

putils = require ("linking-utils")
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
            putils:unwrap_find_target_event (event)

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

        log:info (si, string.format ("handling item: %s (%s)",
            tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

        local exclusive = cutils.parseBool (si_props ["node.exclusive"])
        local passive = cutils.parseBool (si_props ["node.passive"]) or
            cutils.parseBool (target_props ["node.passive"])

        -- break rescan if tried more than 5 times with same target
        if si_flags.failed_peer_id ~= nil and
            si_flags.failed_peer_id == target.id and
            si_flags.failed_count ~= nil and
            si_flags.failed_count > 5 then
          transition:return_error ("tried to link on last rescan, not retrying "
              .. tostring (si_link))
        end

        if si_props ["item.factory.name"] == "si-audio-virtual" then
          if si_props ["item.node.direction"] == "output" then
            -- playback
            out_item = target
            in_item = si
          else
            -- capture
            in_item = target
            out_item = si
          end
        else
          if si_props ["item.node.direction"] == "output" then
            -- playback
            out_item = si
            in_item = target
          else
            -- capture
            in_item = si
            out_item = target
          end
        end

        local is_virtual_client_link = target_props ["item.factory.name"] == "si-audio-virtual"

        log:info (si,
          string.format ("link %s <-> %s passive:%s, passthrough:%s, exclusive:%s, virtual-client:%s",
            tostring (si_props ["node.name"]),
            tostring (target_props ["node.name"]),
            tostring (passive),
            tostring (passthrough),
            tostring (exclusive),
            tostring (is_virtual_client_link)))

        -- create and configure link
        si_link = SessionItem ("si-standard-link")
        if not si_link:configure {
          ["out.item"] = out_item,
          ["in.item"] = in_item,
          ["passive"] = passive,
          ["passthrough"] = passthrough,
          ["exclusive"] = exclusive,
          ["out.item.port.context"] = "output",
          ["in.item.port.context"] = "input",
          ["media.role"] = target_props["role"],
          ["target.media.class"] = target_props["media.class"],
          ["is.virtual.client.link"] = is_virtual_client_link,
          ["main.item.id"] = si.id,
          ["target.item.id"] = target.id,
        } then
          transition:return_error ("failed to configure si-standard-link "
            .. tostring (si_link))
          return
        end

        si_link:connect("link-error", function (_, error_msg)
          local ids = {si.id, si_flags.peer_id}

          for _, id in ipairs (ids) do
            local si = om:lookup {
              Constraint { "id", "=", id, type = "gobject" },
            }
            if si then
              local node = si:get_associated_proxy ("node")
              local client_id = node.properties["client.id"]
              if client_id then
                local client = om:lookup {
                  Constraint { "bound-id", "=", client_id, type = "gobject" }
                }
                if client then
                  log:info (node, "sending client error: " .. error_msg)
                  client:send_error (node["bound-id"], -32, error_msg)
                end
              end
            end
          end
        end)

        -- register
        si_flags.peer_id = target.id
        si_flags.failed_peer_id = target.id
        if si_flags.failed_count ~= nil then
          si_flags.failed_count = si_flags.failed_count + 1
        else
          si_flags.failed_count = 1
        end
        si_link:register ()

        log:info (si_link, "registered virtual si-standard-link between "
                .. tostring (si).." and ".. tostring(target))

        -- only activate non virtual links because virtual links activation is
        -- handled by rescan-virtual-links.lua
        if not is_virtual_client_link then
          si_link:activate (Feature.SessionItem.ACTIVE, function (l, e)
            if e then
              transition:return_error ("failed to activate si-standard-link: "
                  .. tostring (si) .. " error:" .. tostring (e))
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

              log:info (l, "activated si-standard-link between "
                .. tostring (si).." and ".. tostring(target))

              transition:advance ()
            end
          end)
        else
          transition:advance ()
        end
      end,
    },
  },
}:register ()
