-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target node is a filter target.

lutils = require ("linking-utils")
cutils = require ("common-utils")
futils = require ("filter-utils")
log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/get-filter-from-target",
  after = "linking/find-best-target",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source, om, si, si_props, si_flags, target =
        lutils:unwrap_select_target_event (event)

    -- bypass the hook if the target was not found or if the target is media role node
    if target == nil or target.properties["device.intended-roles"] then
      return
    end

    -- bypass the hook if the session item is a smart filter
    local node = si:get_associated_proxy ("node")
    local node_props = node.properties
    local link_group = node_props ["node.link-group"]
    local target_direction = cutils.getTargetDirection (si.properties)
    if link_group ~= nil and
        futils.is_filter_smart (target_direction, link_group) then
      return
    end

    -- bypass the hook if target is defined, is a filter and is targetable
    local target_node = target:get_associated_proxy ("node")
    local target_node_props = target_node.properties
    local target_link_group = target_node_props ["node.link-group"]
    if target_link_group ~= nil and si_flags.has_defined_target then
      if futils.is_filter_smart (target_direction, target_link_group) and
          not futils.is_filter_disabled (target_direction, target_link_group) and
          futils.is_filter_targetable (target_direction, target_link_group) then
        return
      end
    end

    -- Get the filter from the given target if it exists, otherwise get the
    -- default filter, but only if target was not defined
    local media_type = si_props["media.type"]
    local filter_target = futils.get_filter_from_target (target_direction, media_type, target)
    if filter_target ~= nil then
      target = filter_target
      log:info (si, "... got filter for given target")
    elseif filter_target == nil and not si_flags.has_defined_target then
      filter_target = futils.get_filter_from_target (target_direction, media_type, nil)
      if filter_target ~= nil then
        target = filter_target
        log:info (si, "... got default filter for given target")
      end
    end

    local can_passthrough, passthrough_compatible
    if target ~= nil then
      passthrough_compatible, can_passthrough =
      lutils.checkPassthroughCompatibility (si, target)
      if lutils.canLink (si_props, target) and passthrough_compatible then
        target_picked = true;
      end
    end

    if target_picked then
      log:info (si,
        string.format ("... target picked: %s (%s), can_passthrough:%s",
          tostring (target.properties ["node.name"]),
          tostring (target.properties ["node.id"]),
          tostring (can_passthrough)))
      si_flags.can_passthrough = can_passthrough
      event:set_data ("target", target)
    end
  end
}:register ()
