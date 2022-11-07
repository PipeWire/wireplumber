-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Links a session item to the target that has been previously selected.
-- This is meant to be the last hook in the find-target-si-and-link chain.

local putils = require ("policy-utils")
local cutils = require ("common-utils")

SimpleEventHook {
  name = "link-target@policy-desktop",
  type = "on-event",
  priority = HookPriority.ULTRA_LOW,
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "find-target-si-and-link" },
    },
  },
  execute = function (event)
    local si = event:get_subject ()
    local si_id = si.id
    local si_flags = putils.get_flags (si_id)
    local si_target = si_flags.si_target

    if not si_target then
      -- bypass the hook, nothing to link to.
      return
    end

    local si_props = si.properties
    local target_props = si_target.properties
    local out_item = nil
    local in_item = nil
    local si_link = nil
    local passthrough = si_flags.can_passthrough

    Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
      tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

    local exclusive = parseBool (si_props ["node.exclusive"])
    local passive = parseBool (si_props ["node.passive"]) or
        parseBool (target_props ["node.passive"])

    -- break rescan if tried more than 5 times with same target
    if si_flags.failed_peer_id ~= nil and
        si_flags.failed_peer_id == si_target.id and
        si_flags.failed_count ~= nil and
        si_flags.failed_count > 5 then
      Log.warning (si, "tried to link on last rescan, not retrying")
      goto done
    end

    if si_props ["item.node.direction"] == "output" then
      -- playback
      out_item = si
      in_item = si_target
    else
      -- capture
      in_item = si
      out_item = si_target
    end

    Log.info (si,
      string.format ("link %s <-> %s passive:%s, passthrough:%s, exclusive:%s",
        tostring (si_props ["node.name"]),
        tostring (target_props ["node.name"]),
        tostring (passive), tostring (passthrough), tostring (exclusive)))

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
      ["is.policy.item.link"] = true,
    } then
      Log.warning (si_link, "failed to configure si-standard-link")
      goto done
    end

    -- register
    si_flags.peer_id = si_target.id
    si_flags.failed_peer_id = si_target.id
    if si_flags.failed_count ~= nil then
      si_flags.failed_count = si_flags.failed_count + 1
    else
      si_flags.failed_count = 1
    end
    si_link:register ()

    -- activate
    si_link:activate (Feature.SessionItem.ACTIVE, function (l, e)
      if e then
        Log.info (l, "failed to activate si-standard-link: " .. tostring (si) .. " error:" .. tostring (e))
        if si_flags ~= nil then
          si_flags.peer_id = nil
        end
        l:remove ()
      else
        if si_flags ~= nil then
          si_flags.failed_peer_id = nil
          if si_flags.peer_id == nil then
            si_flags.peer_id = si_target.id
          end
          si_flags.failed_count = 0
        end
        Log.info (l, "activated si-standard-link " .. tostring (si))
      end
      putils.set_flags (si_id, si_flags)
    end)

    ::done::
    si_flags.was_handled = true
    putils.set_flags (si_id, si_flags)
    putils.checkFollowDefault (si, si_target, si_flags.has_node_defined_target)
  end
}:register ()
