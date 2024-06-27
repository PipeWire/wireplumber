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

function findFilterTarget (si, om)
  local node = si:get_associated_proxy ("node")
  local link_group = node.properties ["node.link-group"]
  local target_id = -1

  -- return nil if session item is not a filter node
  if link_group == nil then
    return nil, false
  end

  -- return nil if filter is not smart
  local direction = cutils.getTargetDirection (si.properties)
  if not futils.is_filter_smart (direction, link_group) then
    return nil, false
  end

  -- get the filter target
  return futils.get_filter_target (direction, link_group), true
end

SimpleEventHook {
  name = "linking/find-filter-target",
  after = "linking/find-defined-target",
  before = "linking/prepare-link",
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

    local dont_fallback = cutils.parseBool (si_props ["node.dont-fallback"])
    local target_picked = false
    local allow_fallback

    log:info (si, string.format ("handling item %d: %s (%s)", si.id,
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    target, is_smart_filter = findFilterTarget (si, om)

    local can_passthrough, passthrough_compatible
    if target then
      passthrough_compatible, can_passthrough =
          lutils.checkPassthroughCompatibility (si, target)
      if lutils.canLink (si_props, target) and passthrough_compatible then
        target_picked = true
      end
    end

    if target_picked and target then
      log:info (si,
        string.format ("... filter target picked: %s (%s), can_passthrough:%s",
          tostring (target.properties ["node.name"]),
          tostring (target.properties ["node.id"]),
          tostring (can_passthrough)))
      si_flags.can_passthrough = can_passthrough
      event:set_data ("target", target)
    elseif is_smart_filter and dont_fallback then
      -- send error to client and destroy node if linger is not set
      local linger = cutils.parseBool (si_props ["node.linger"])
      if not linger then
        local node = si:get_associated_proxy ("node")
        lutils.sendClientError (event, node, -2, "smart filter defined target not found")
        node:request_destroy ()
        log:info(si, "... destroyed node as smart filter defined target was not found")
      else
        log:info(si, "... waiting for smart filter defined target as dont-fallback is set")
      end
      event:stop_processing ()
    end
  end
}:register ()
