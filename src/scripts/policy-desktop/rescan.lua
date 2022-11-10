-- WirePlumber
--
-- Copyright © 2020-2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Handle new linkables and trigger rescanning of the graph.
-- Rescan the graph by pushing new find-target-si-and-link events for
-- all linkables that need to be linked
-- Cleanup links when the linkables they are associated with are removed.
-- Also, cleanup flags attached to linkables.

local putils = require ("policy-utils")
local cutils = require ("common-utils")

function checkLinkable (si, om, handle_nonstreams)
  -- only handle stream session items
  local si_props = si.properties
  if not si_props or (si_props ["item.node.type"] ~= "stream"
      and not handle_nonstreams) then
    return false
  end

  -- Determine if we can handle item by this policy
  if om:lookup { type = "SiEndpoint" } and
      si_props ["item.factory.name"] == "si-audio-adapter" then
    return false
  end

  return true, si_props
end

SimpleEventHook {
  name = "linkable-removed@policy-node",
  type = "on-event",
  priority = HookPriority.VERY_LOW,
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "object-removed" },
      Constraint { "event.subject.type", "=", "linkable" },
    },
  },
  execute = function (event)
    local si = event:get_subject ()
    local source = event:get_source ()
    local om = source ["object-manager"]
    local si_id = si.id
    local valid, si_props = checkLinkable (si, om, true)
    if not valid then
      return
    end

    Log.info (si, string.format ("unhandling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    -- iterate over all the links in the graph and
    -- remove any links associated with this item
    for silink in om:iterate { type = "SiLink" } do
      local out_id = tonumber (silink.properties ["out.item.id"])
      local in_id = tonumber (silink.properties ["in.item.id"])

      if out_id == si_id or in_id == si_id then
        local in_flags = putils:get_flags (in_id)
        local out_flags = putils:get_flags (out_id)

        if out_id == si_id and in_flags.peer_id == out_id then
          in_flags.peer_id = nil
        elseif in_id == si_id and out_flags.peer_id == in_id then
          out_flags.peer_id = nil
        end

        silink:remove ()
        Log.info (silink, "... link removed")
      end
    end

    putils:clear_flags (si_id)
  end
}:register ()

SimpleEventHook {
  name = "rescan-session@policy-desktop",
  type = "on-event",
  -- let higher priority hooks deal with preparing the graph first
  -- (things like finding the default nodes, etc...)
  priority = HookPriority.VERY_LOW,
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "rescan-session" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local om = source ["object-manager"]

    Log.info ("rescanning...")

    for si in om:iterate { type = "SiLinkable" } do
      local valid, si_props = checkLinkable (si, om)
      if not valid then
        goto skip_linkable
      end

      -- check if we need to link this node at all
      local autoconnect = cutils.parseBool (si_props ["node.autoconnect"])
      if not autoconnect then
        Log.debug (si, tostring (si_props ["node.name"]) .. " does not need to be autoconnected")
        goto skip_linkable
      end

      -- push event to find target and link
      source:call ("push-event", "find-target-si-and-link", si, nil)

    ::skip_linkable::
    end
  end
}:register ()

SimpleEventHook {
  name = "rescan-trigger@policy-desktop",
  -- go with a low priority to allow NORMAL handlers
  -- to stop the event and prevent the rescan
  priority = HookPriority.VERY_LOW,
  type = "on-event",
  interests = {
    -- on linkable added or removed, where linkable is adapter or plain node
    EventInterest {
      Constraint { "event.type", "c", "object-added", "object-removed" },
      Constraint { "event.subject.type", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    },
    -- on device Routes changed
    EventInterest {
      Constraint { "event.type", "=", "params-changed" },
      Constraint { "event.subject.type", "=", "device" },
      Constraint { "event.subject.param-id", "=", "Route" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("schedule-rescan")
  end
}:register ()
