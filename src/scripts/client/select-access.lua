-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Triggers select-access event for added clients.

SimpleEventHook {
  name = "client/select-access-trigger",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "client-added" },
    },
  },
  execute = function(event)
    local source = event:get_source ()
    local client = event:get_subject ()
    source:call ("push-event", "select-access", client, nil)
  end
}:register()
