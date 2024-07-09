-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Check if the target node is a filter target.

lutils = require ("linking-utils")
cutils = require ("common-utils")
agutils = require ("audio-group-utils")

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-audio-group-target",
  after = "linking/find-defined-target",
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
    local node = nil
    local audio_group = nil

    log:info (si, string.format ("handling item %d: %s (%s)", si.id,
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    -- Get associated node
    node = si:get_associated_proxy ("node")
    if node == nil then
      return
    end

    -- audio group
    audio_group = agutils.get_audio_group (node)
    if audio_group == nil then
      return
    end

    -- find the target with same audio group, if any
    for target in om:iterate {
      type = "SiLinkable",
      Constraint { "item.node.type", "=", "device" },
      Constraint { "item.node.direction", "=", target_direction },
      Constraint { "media.type", "=", si_props ["media.type"] },
    } do
      target_node = target:get_associated_proxy ("node")
      target_node_props = target_node.properties
      target_audio_group = target_node_props ["session.audio-group"]

      if target_audio_group == nil then
        goto skip_linkable
      end

      if target_audio_group ~= audio_group then
        goto skip_linkable
      end

      local passthrough_compatible, can_passthrough =
          lutils.checkPassthroughCompatibility (si, target)
      if not passthrough_compatible then
        log:debug ("... passthrough is not compatible, skip linkable")
        goto skip_linkable
      end

      target_picked = target
      target_can_passthrough = can_passthrough
      break

      ::skip_linkable::
    end

    -- set target
    if target_picked then
      log:info (si,
        string.format ("... audio group target picked: %s (%s), can_passthrough:%s",
          tostring (target_picked.properties ["node.name"]),
          tostring (target_picked.properties ["node.id"]),
          tostring (target_can_passthrough)))
      si_flags.can_passthrough = target_can_passthrough
      event:set_data ("target", target_picked)
    end
  end
}:register ()
