-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target node is defined explicitly.
-- This definition can be done in two ways.
-- 1. "node.target"/"target.object" in the node properties
-- 2. "target.node"/"target.object" in the default metadata

lutils = require ("linking-utils")
cutils = require ("common-utils")
log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-defined-target",
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

    log:info (si, string.format ("handling item %d: %s (%s)", si.id,
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    local metadata = Settings.get_boolean ("linking.allow-moving-streams") and
        cutils.get_default_metadata_object ()
    local dont_fallback = cutils.parseBool (si_props ["node.dont-fallback"])
    local dont_move = cutils.parseBool (si_props ["node.dont-move"])
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

    if metadata and not dont_move then
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
      if target and lutils.canLink (si_props, target) then
        target_picked = true
      end
    elseif target_value then
      for lnkbl in om:iterate { type = "SiLinkable" } do
        local target_props = lnkbl.properties
        if (target_props ["node.name"] == target_value or
            target_props ["object.path"] == target_value) and
            target_props ["item.node.direction"] == cutils.getTargetDirection (si_props) and
            lutils.canLink (si_props, lnkbl) then
          target_picked = true
          target = lnkbl
          break
        end
      end
    end

    local can_passthrough, passthrough_compatible
    if target then
      passthrough_compatible, can_passthrough =
      lutils.checkPassthroughCompatibility (si, target)
      if not passthrough_compatible then
        target = nil
      end
    end

    si_flags.has_defined_target = false
    if target_picked and target then
      log:info (si,
        string.format ("... defined target picked: %s (%s), can_passthrough:%s",
          tostring (target.properties ["node.name"]),
          tostring (target.properties ["node.id"]),
          tostring (can_passthrough)))
      si_flags.has_node_defined_target = node_defined
      si_flags.can_passthrough = can_passthrough
      si_flags.has_defined_target = true
      event:set_data ("target", target)
    elseif target_value and dont_fallback then
      -- send error to client and destroy node if linger is not set
      local linger = cutils.parseBool (si_props ["node.linger"])
      if not linger then
        local node = si:get_associated_proxy ("node")
        lutils.sendClientError (event, node, -2, "defined target not found")
        node:request_destroy ()
        log:info(si, "... destroyed node as defined target was not found")
      else
        log:info(si, "... waiting for defined target as dont-fallback is set")
      end
      event:stop_processing ()
    end

  end
}:register ()
