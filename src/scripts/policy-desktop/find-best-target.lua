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
  after = "find-default-target@policy-desktop",
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

    local target_direction = cutils.getTargetDirection (si_props)
    local target_picked = nil
    local target_can_passthrough = false
    local target_priority = 0
    local target_plugged = 0

    Log.info (si, string.format ("handling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    for target in om:iterate {
      type = "SiLinkable",
      Constraint { "item.node.type", "=", "device" },
      Constraint { "item.node.direction", "=", target_direction },
      Constraint { "media.type", "=", si_props ["media.type"] },
    } do
      local target_props = target.properties
      local target_node_id = target_props ["node.id"]
      local priority = tonumber (target_props ["priority.session"]) or 0

      Log.debug (string.format ("Looking at: %s (%s)",
        tostring (target_props ["node.name"]),
        tostring (target_node_id)))

      if not putils.canLink (si_props, target) then
        Log.debug ("... cannot link, skip linkable")
        goto skip_linkable
      end

      if not putils.haveAvailableRoutes (target_props) then
        Log.debug ("... does not have routes, skip linkable")
        goto skip_linkable
      end

      local passthrough_compatible, can_passthrough =
      putils.checkPassthroughCompatibility (si, target)
      if not passthrough_compatible then
        Log.debug ("... passthrough is not compatible, skip linkable")
        goto skip_linkable
      end

      local plugged = tonumber (target_props ["item.plugged.usec"]) or 0

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
        target_picked = target
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
      si_flags.can_passthrough = target_can_passthrough
      event:set_data ("target", target_picked)
    end
  end
}:register ()
