-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

sources = {}

SimpleEventHook {
  name = "node/suspend-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-state-changed" },
      Constraint { "media.class", "matches", "Audio/*" },
    },
    EventInterest {
      Constraint { "event.type", "=", "node-state-changed" },
      Constraint { "media.class", "matches", "Video/*" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local new_state = event:get_properties ()["event.subject.new-state"]

    -- Always clear the current source if any
    local id = node["bound-id"]
    if sources[id] then
      sources[id]:destroy()
      sources[id] = nil
    end

    -- Add a timeout source if idle for at least 5 seconds
    if new_state == "idle" or new_state == "error" then
      -- honor "session.suspend-timeout-seconds" if specified
      local timeout =
          tonumber(node.properties["session.suspend-timeout-seconds"]) or 5

      if timeout == 0 then
        return
      end

      -- add idle timeout; multiply by 1000, timeout_add() expects ms
      sources[id] = Core.timeout_add(timeout * 1000, function()
        -- Suspend the node
        Log.info(node, "was idle for a while; suspending ...")
        node:send_command("Suspend")

        -- Unref the source
        sources[id] = nil

        -- false (== G_SOURCE_REMOVE) destroys the source so that this
        -- function does not get fired again after 5 seconds
        return false
      end)
    end
  end
}:register ()
