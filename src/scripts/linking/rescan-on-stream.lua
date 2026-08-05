-- WirePlumber
--
-- Copyright © 2026 Axis Communications AB.
--
-- SPDX-License-Identifier: MIT
--
-- Trigger a full rescan when streams are added or removed.
-- This can be disabled by setting hooks.link.rescan-on-stream = disabled
-- in wireplumber.profiles.

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/rescan-trigger-on-stream-added-removed",
  interests = {
    EventInterest {
      Constraint { "event.type", "c", "stream-added", "stream-removed" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("schedule-rescan", "linking")
  end
}:register ()
