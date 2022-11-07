-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Traverse through all the possible targets to pick up target node.

local putils = require ("policy-utils")
local cutils = require ("common-utils")

SimpleEventHook {
  name = "find-best-target@policy-desktop",
  type = "on-event",
  priority = HookPriority.LOW,
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

    if si_target then
      -- bypass the hook as the target is already picked up.
      return
    end

    local si_props = si.properties
    local target_direction = cutils.getTargetDirection (si_props)
    local target_picked = nil
    local target_can_passthrough = false
    local target_priority = 0
    local target_plugged = 0

    Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
      tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

    for si_target in linkables_om:iterate {
      Constraint { "item.node.type", "=", "device" },
      Constraint { "item.node.direction", "=", target_direction },
      Constraint { "media.type", "=", si_props ["media.type"] },
    } do
      local si_target_props = si_target.properties
      local si_target_node_id = si_target_props ["node.id"]
      local priority = tonumber (si_target_props ["priority.session"]) or 0

      Log.debug (string.format ("Looking at: %s (%s)",
        tostring (si_target_props ["node.name"]),
        tostring (si_target_node_id)))

      if not putils.canLink (si_props, si_target) then
        Log.debug ("... cannot link, skip linkable")
        goto skip_linkable
      end

      if not putils.haveAvailableRoutes (si_target_props) then
        Log.debug ("... does not have routes, skip linkable")
        goto skip_linkable
      end

      local passthrough_compatible, can_passthrough =
      putils.checkPassthroughCompatibility (si, si_target)
      if not passthrough_compatible then
        Log.debug ("... passthrough is not compatible, skip linkable")
        goto skip_linkable
      end

      local plugged = tonumber (si_target_props ["item.plugged.usec"]) or 0

      Log.debug ("... priority:" .. tostring (priority) .. ", plugged:" .. tostring (plugged))

      -- (target_picked == NULL) --> make sure atleast one target is picked.
      -- (priority > target_priority) --> pick the highest priority linkable(node)
      -- target.
      -- (priority == target_priority and plugged > target_plugged) --> pick the
      -- latest connected/plugged(in time) linkable(node) target.
      if (target_picked == nil or
          priority > target_priority or
          (priority == target_priority and plugged > target_plugged)) then
        Log.debug ("... picked")
        target_picked = si_target
        target_can_passthrough = can_passthrough
        target_priority = priority
        target_plugged = plugged
      end
      ::skip_linkable::
    end

    if target_picked then
      Log.info (si,
        string.format ("... best target picked: %s (%s), can_passthrough:%s",
          tostring (target_picked.properties ["node.name"]),
          tostring (target_picked.properties ["node.id"]),
          tostring (target_can_passthrough)))
      si_flags.si_target = target_picked
      si_flags.can_passthrough = target_can_passthrough
    else
      si_flags.si_target = nil
      si_flags.can_passthrough = nil
    end

    putils.set_flags (si_id, si_flags)
  end
}:register ()
