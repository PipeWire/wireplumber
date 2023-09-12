-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target node is a filter target.

local putils = require ("linking-utils")
local cutils = require ("common-utils")
local futils = require ("filter-utils")
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
        putils:unwrap_find_target_event (event)

    -- bypass the hook if the target was not found
    if target == nil then
      return
    end

    -- bypass the hook if the session item is a filter
    local node = si:get_associated_proxy ("node")
    local link_group = node.properties ["node.link-group"]
    if link_group ~= nil then
      return
    end

    -- Get the filter from the given target, if any
    local target_direction = cutils.getTargetDirection (si.properties)
    local filter_target = futils.get_filter_from_target (target_direction, target)
    if filter_target ~= nil then
      target = filter_target
    end

    local can_passthrough, passthrough_compatible
    if target ~= nil then
      passthrough_compatible, can_passthrough =
      putils.checkPassthroughCompatibility (si, target)
      if putils.canLink (si_props, target) and passthrough_compatible then
        target_picked = true;
      end
    end

    if target_picked then
      log:info (si,
        string.format ("... filter target picked: %s (%s), can_passthrough:%s",
          tostring (target.properties ["node.name"]),
          tostring (target.properties ["node.id"]),
          tostring (can_passthrough)))
      si_flags.can_passthrough = can_passthrough
      event:set_data ("target", target)
    end
  end
}:register ()
