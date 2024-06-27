-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Traverse through all the possible targets to pick up target node.

lutils = require ("linking-utils")
cutils = require ("common-utils")
futils = require ("filter-utils")
log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-best-target",
  after = "linking/find-default-target",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source, om, si, si_props, si_flags, target =
        lutils:unwrap_select_target_event (event)

    -- bypass the hook if the target is already picked up
    if target then
      return
    end

    local target_direction = cutils.getTargetDirection (si_props)
    local target_picked = nil
    local target_can_passthrough = false
    local target_priority = 0
    local target_plugged = 0

    log:info (si, string.format ("handling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    for target in om:iterate {
      type = "SiLinkable",
      Constraint { "item.node.type", "=", "device" },
      Constraint { "item.node.direction", "=", target_direction },
      Constraint { "media.type", "=", si_props ["media.type"] },
    } do
      local target_props = target.properties
      local target_node_id = target_props ["node.id"]
      local si_target_node = target:get_associated_proxy ("node")
      local si_target_link_group = si_target_node.properties ["node.link-group"]
      local priority = tonumber (target_props ["priority.session"]) or 0

      log:debug (string.format ("Looking at: %s (%s)",
        tostring (target_props ["node.name"]),
        tostring (target_node_id)))

      -- Skip smart filters as best target
      if si_target_link_group ~= nil and
          futils.is_filter_smart (target_direction, si_target_link_group) then
        Log.debug ("... ignoring smart filter as best target")
        goto skip_linkable
      end

      if not lutils.canLink (si_props, target) then
        log:debug ("... cannot link, skip linkable")
        goto skip_linkable
      end

      if not lutils.haveAvailableRoutes (target_props) then
        log:debug ("... does not have routes, skip linkable")
        goto skip_linkable
      end

      local passthrough_compatible, can_passthrough =
      lutils.checkPassthroughCompatibility (si, target)
      if not passthrough_compatible then
        log:debug ("... passthrough is not compatible, skip linkable")
        goto skip_linkable
      end

      local plugged = tonumber (target_props ["item.plugged.usec"]) or 0

      log:debug ("... priority:" .. tostring (priority) .. ", plugged:" .. tostring (plugged))

      -- (target_picked == NULL) --> make sure atleast one target is picked.
      -- (priority > target_priority) --> pick the highest priority linkable(node)
      -- target.
      -- (priority == target_priority and plugged > target_plugged) --> pick the
      -- latest connected/plugged(in time) linkable(node) target.
      if (target_picked == nil or
          priority > target_priority or
          (priority == target_priority and plugged > target_plugged)) then
        log:debug ("... picked")
        target_picked = target
        target_can_passthrough = can_passthrough
        target_priority = priority
        target_plugged = plugged
      end
      ::skip_linkable::
    end

    if target_picked then
      log:info (si,
        string.format ("... best target picked: %s (%s), can_passthrough:%s",
          tostring (target_picked.properties ["node.name"]),
          tostring (target_picked.properties ["node.id"]),
          tostring (target_can_passthrough)))
      si_flags.can_passthrough = target_can_passthrough
      event:set_data ("target", target_picked)
    end
  end
}:register ()
