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
    local source, om, si, si_props, si_flags, target =
        putils:unwrap_find_target_event (event)

    -- bypass the hook if the target is already picked up
    if target then
      return
    end

    local target_picked = false

    Log.info (si, string.format ("handling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    target = putils.findDefaultLinkable (si)

    local can_passthrough, passthrough_compatible
    if target then
      passthrough_compatible, can_passthrough =
      putils.checkPassthroughCompatibility (si, target)
      if putils.canLink (si_props, target) and passthrough_compatible then
        target_picked = true;
      end
    end

    if target_picked then
      Log.info (si,
        string.format ("... default target picked: %s (%s), can_passthrough:%s",
          tostring (target.properties ["node.name"]),
          tostring (target.properties ["node.id"]),
          tostring (can_passthrough)))
      si_flags.can_passthrough = can_passthrough
      event:set_data ("target", target)
    end
  end
}:register ()
