-- WirePlumber
--
-- Copyright © 2024 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Select the media role target

cutils = require("common-utils")
lutils = require("linking-utils")
futils = require("filter-utils")
log = Log.open_topic("s-linking")

SimpleEventHook {
  name = "linking/find-media-role-target",
  after = { "linking/find-defined-target",
            "linking/find-filter-target" },
  before = "linking/prepare-link",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local _, om, si, si_props, _, target =
        lutils:unwrap_select_target_event (event)

    local target_direction = cutils.getTargetDirection (si_props)
    local media_role = si_props["media.role"]

    -- bypass the hook if the target is already picked up or if the role is not
    -- defined
    if target or media_role == nil then
      return
    end

    -- bypass the hook if the stream is a smart filter
    local si_link_group = si_props["node.link-group"]
    if si_link_group and
        futils.is_filter_smart (target_direction, si_link_group) then
      return
    end

    log:info (si, string.format ("handling item %d: %s (%s) role (%s)", si.id,
      tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), media_role))

    for si_target in om:iterate {
      type = "SiLinkable",
      Constraint { "item.node.direction", "=", target_direction },
      Constraint { "device.intended-roles", "+" },
      Constraint { "media.type", "=", si_props["media.type"] },
    } do

      local roles_json = si_target.properties["device.intended-roles"]
      local roles_table = Json.Raw(roles_json):parse()

      for _, target_role in ipairs(roles_table) do
        if target_role == media_role and lutils.canLink (si_props, si_target) then
          target = si_target
          break
        end
      end

      if target then
        break
      end
    end

    -- set target
    if target ~= nil then
      log:info(si,
        string.format("... media role target picked: %s (%s)",
          tostring(target.properties["node.name"]),
          tostring(target.properties["node.id"])))
      event:set_data("target", target)
    end
  end
}:register()
