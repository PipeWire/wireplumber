-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target node is a filter target.

putils = require ("linking-utils")
cutils = require ("common-utils")
futils = require ("filter-utils")
log = Log.open_topic ("s-linking")

function findFilterTarget (si, om)
  local node = si:get_associated_proxy ("node")
  local direction = cutils.getTargetDirection (si.properties)
  local link_group = node.properties ["node.link-group"]
  local target_id = -1

  -- return nil if session item is not a filter node
  if link_group == nil then
    return nil
  end

  -- return nil if filter is not smart
  if not futils.is_filter_smart (direction, link_group) then
    return nil
  end

  -- get the filter target
  return futils.get_filter_target (direction, link_group)
end

SimpleEventHook {
  name = "linking/find-filter-target",
  after = "linking/find-defined-target",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source, om, si, si_props, si_flags, target =
        putils:unwrap_select_target_event (event)

    -- bypass the hook if the target is already picked up
    if target then
      return
    end

    local target_picked = false

    log:info (si, string.format ("handling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    target = findFilterTarget (si, om)

    local can_passthrough, passthrough_compatible
    if target then
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
