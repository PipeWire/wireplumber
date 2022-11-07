-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target node is defined explicitly.
-- This defination can be done in two ways.
-- 1. "node.target"/"target.object" in the node properties
-- 2. "target.node"/"target.object" in the default metadata

local putils = require ("policy-utils")
local cutils = require ("common-utils")

SimpleEventHook {
  name = "find-defined-target@policy-desktop",
  type = "on-event",
  priority = HookPriority.HIGH,
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "find-target-si-and-link" },
    },
  },
  execute = function (event)
    local si = event:get_subject ()
    local si_props = si.properties
    local si_id = si.id;
    local si_flags = putils.get_flags (si_id)
    local si_target = nil

    Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
      tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

    local metadata = config.move and putils.get_default_metadata_object ()
    local target_key
    local target_value = nil
    local node_defined = false
    local target_picked = nil

    si_flags.node_name = si_props ["node.name"]
    si_flags.node_id = si_props ["node.id"]

    if si_props ["target.object"] ~= nil then
      target_value = si_props ["target.object"]
      target_key = "object.serial"
      node_defined = true
    elseif si_props ["node.target"] ~= nil then
      target_value = si_props ["node.target"]
      target_key = "node.id"
      node_defined = true
    end

    if metadata then
      local id = metadata:find (si_props ["node.id"], "target.object")
      if id ~= nil then
        target_value = id
        target_key = "object.serial"
        node_defined = false
      else
        id = metadata:find (si_props ["node.id"], "target.node")
        if id ~= nil then
          target_value = id
          target_key = "node.id"
          node_defined = false
        end
      end
    end

    if target_value == "-1" then
      target_picked = false
      si_target = nil
    elseif target_value and tonumber (target_value) then
      si_target = linkables_om:lookup {
        Constraint { target_key, "=", target_value },
      }
      if si_target and putils.canLink (si_props, si_target) then
        target_picked = true
      end
    elseif target_value then
      for lnkbl in linkables_om:iterate () do
        local target_props = lnkbl.properties
        if (target_props ["node.name"] == target_value or
            target_props ["object.path"] == target_value) and
            target_props ["item.node.direction"] == cutils.getTargetDirection (si_props) and
            putils.canLink (si_props, lnkbl) then
          target_picked = true
          si_target = lnkbl
          break
        end

      end
    end

    local can_passthrough, passthrough_compatible
    if si_target then
      passthrough_compatible, can_passthrough =
      putils.checkPassthroughCompatibility (si, si_target)

      if not passthrough_compatible then
        si_target = nil
      end
    end

    -- if the client has seen a target that we haven't yet prepared, stop the
    -- event and wait(for one time) for next rescan to happen and hope for the
    -- best.

    if target_picked
        and not si_target
        and not si_flags.was_handled
        and not si_flags.done_waiting then
      Log.info(si, "... waiting for target")
      si_flags.done_waiting = true
      event:stop_processing ()

    elseif target_picked then
      Log.info (si,
        string.format ("... defined target picked: %s (%s), can_passthrough:%s",
          tostring (si_target.properties ["node.name"]),
          tostring (si_target.properties ["node.id"]),
          tostring (can_passthrough)))
      si_flags.si_target = si_target
      si_flags.has_node_defined_target = node_defined
      si_flags.can_passthrough = can_passthrough
    else
      si_flags.si_target = nil
      si_flags.can_passthrough = nil
      si_flags.has_node_defined_target = nil
    end

    putils.set_flags (si_id, si_flags)
  end
}:register ()
