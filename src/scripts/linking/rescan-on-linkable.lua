-- WirePlumber
--
-- Copyright © 2026 Axis Communications AB.
--
-- SPDX-License-Identifier: MIT
--
-- Trigger a full rescan when linkable session items are added or removed.
-- This can be disabled by setting hooks.linking.rescan-on-linkable = disabled
-- in wireplumber.profiles.

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/rescan-trigger-on-linkable-added-removed",
  interests = {
    EventInterest {
      Constraint { "event.type", "c", "session-item-added", "session-item-removed" },
      Constraint { "event.session-item.interface", "=", "linkable" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("schedule-rescan", "linking")
  end
}:register ()
