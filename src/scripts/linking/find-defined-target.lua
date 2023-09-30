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

putils = require ("linking-utils")
cutils = require ("common-utils")
config = require ("linking-config")
log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-defined-target",
  after = "linking/find-virtual-target",
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

    log:info (si, string.format ("handling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    local metadata = config.move and cutils.get_default_metadata_object ()
    local target_key
    local target_value = nil
    local node_defined = false
    local target_picked = nil

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
      target = nil
    elseif target_value and tonumber (target_value) then
      target = om:lookup {
        type = "SiLinkable",
        Constraint { target_key, "=", target_value },
      }
      if target and putils.canLink (si_props, target) then
        target_picked = true
      end
    elseif target_value then
      for lnkbl in om:iterate { type = "SiLinkable" } do
        local target_props = lnkbl.properties
        if (target_props ["node.name"] == target_value or
            target_props ["object.path"] == target_value) and
            target_props ["item.node.direction"] == cutils.getTargetDirection (si_props) and
            putils.canLink (si_props, lnkbl) then
          target_picked = true
          target = lnkbl
          break
        end

      end
    end

    local can_passthrough, passthrough_compatible
    if target then
      passthrough_compatible, can_passthrough =
      putils.checkPassthroughCompatibility (si, target)

      if not passthrough_compatible then
        target = nil
      end
    end

    -- if the client has seen a target that we haven't yet prepared, stop the
    -- event and wait(for one time) for next rescan to happen and hope for the
    -- best.

    if target_picked
        and not target
        and not si_flags.was_handled
        and not si_flags.done_waiting then
      log:info(si, "... waiting for target")
      si_flags.done_waiting = true
      event:stop_processing ()

    elseif target_picked then
      log:info (si,
        string.format ("... defined target picked: %s (%s), can_passthrough:%s",
          tostring (target.properties ["node.name"]),
          tostring (target.properties ["node.id"]),
          tostring (can_passthrough)))
      si_flags.has_node_defined_target = node_defined
      si_flags.can_passthrough = can_passthrough
      event:set_data ("target", target)
    end
  end
}:register ()
