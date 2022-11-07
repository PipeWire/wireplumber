-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if default nodes can be picked up as target node.

local putils = require ("policy-utils")

SimpleEventHook {
  name = "find-default-target@policy-desktop",
  type = "on-event",
  priority = HookPriority.NORMAL,
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

    if si_target or (default_nodes == nil) then
      -- bypass the hook as the target is already picked up.
      return
    end

    local si_props = si.properties
    local target_picked = false

    Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
      tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

    local si_target = putils.findDefaultLinkable (si)

    local can_passthrough, passthrough_compatible
    if si_target then
      passthrough_compatible, can_passthrough =
      putils.checkPassthroughCompatibility (si, si_target)
      if putils.canLink (si_props, si_target) and passthrough_compatible then
        target_picked = true;
      end
    end

    if target_picked then
      Log.info (si,
        string.format ("... default target picked: %s (%s), can_passthrough:%s",
          tostring (si_target.properties ["node.name"]),
          tostring (si_target.properties ["node.id"]),
          tostring (can_passthrough)))
      si_flags.si_target = si_target
      si_flags.can_passthrough = can_passthrough
    else
      si_flags.si_target = nil
      si_flags.can_passthrough = nil
    end

    putils.set_flags (si_id, si_flags)
  end
}:register ()
