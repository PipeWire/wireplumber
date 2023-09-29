-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Select the virtual target based on roles

putils = require ("linking-utils")
log = Log.open_topic ("s-linking")

defaults = {}
defaults.roles = Json.Object {}

config = {}
config.roles = Conf.get_section (
    "virtual-item-roles", defaults.roles):parse ()

function findRole(role, tmc)
  if role and not config.roles[role] then
    -- find the role with matching alias
    for r, p in pairs(config.roles) do
      -- default media class can be overridden in the role config data
      mc = p["media.class"] or "Audio/Sink"
      if (type(p.alias) == "table" and tmc == mc) then
        for i = 1, #(p.alias), 1 do
          if role == p.alias[i] then
            return r
          end
        end
      end
    end

    -- otherwise get the lowest priority role
    local lowest_priority_p = nil
    local lowest_priority_r = nil
    for r, p in pairs(config.roles) do
      mc = p["media.class"] or "Audio/Sink"
      if tmc == mc and (lowest_priority_p == nil or
          p.priority < lowest_priority_p.priority) then
        lowest_priority_p = p
        lowest_priority_r = r
      end
    end
    return lowest_priority_r
  end
  return role
end

SimpleEventHook {
  name = "linking/find-virtual-target",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source, om, si, si_props, si_flags, target =
        putils:unwrap_find_target_event (event)
    local target_class_assoc = {
      ["Stream/Input/Audio"] = "Audio/Source",
      ["Stream/Output/Audio"] = "Audio/Sink",
      ["Stream/Input/Video"] = "Video/Source",
    }
    local node = si:get_associated_proxy ("node")
    local highest_priority = -1
    local target = nil
    local role = node.properties["media.role"] or "Default"

    -- bypass the hook if the target is already picked up
    if target then
      return
    end

    -- dont use virtual target for any si-audio-virtual
    if si_props ["item.factory.name"] == "si-audio-virtual" then
      return
    end

    log:info (si, string.format ("handling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    -- get target media class
    local target_media_class = target_class_assoc[si_props ["media.class"]]
    if not target_media_class then
      log:info (si, "target media class not found")
      return
    end

    -- find highest priority virtual by role
    local media_role = findRole (role, target_media_class)
    if media_role == nil then
      log:info (si, "media role not found")
      return
    end

    for si_virtual in om:iterate {
      Constraint { "role", "=", media_role, type = "pw-global" },
      Constraint { "media.class", "=", target_media_class, type = "pw-global" },
      Constraint { "item.factory.name", "=", "si-audio-virtual", type = "pw-global" },
    } do
      local priority = tonumber(si_virtual.properties["priority"])
      if priority > highest_priority then
        highest_priority = priority
        target = si_virtual
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

    -- set target
    if target ~= nil then
      si_flags.can_passthrough = can_passthrough
      event:set_data ("target", target)
    end
  end
}:register ()
