-- WirePlumber
--
-- Copyright © 2020-2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--

local putils = require ("policy-utils")

function checkLinkable (si, om)
  -- only handle session items that has a node associated proxy
  local node = si:get_associated_proxy ("node")
  if not node or not node.properties then
    return false
  end

  -- only handle stream session items
  local media_class = node.properties["media.class"]
  if not media_class or not string.find (media_class, "Stream") then
    return false
  end

  -- Determine if we can handle item by this policy
  for si_virtual in om:iterate {
    Constraint { "item.factory.name", "=", "si-audio-virtual", type = "pw-global" },
  } do
    return true
  end

  return false
end

SimpleEventHook {
  name = "linkable-client-removed@policy-virtual",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "session-item-removed" },
      Constraint { "event.session-item.interface", "=", "linkable" },
    },
  },
  execute = function (event)
    local si = event:get_subject ()
    local source = event:get_source ()
    local om = source:call ("get-object-manager", "session-item")
    local si_id = si.id
    local valid, si_props = checkLinkable (si, om)
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
  name = "rescan-client-for-virtual-session@policy-virtual",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "rescan-session" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local om = source:call ("get-object-manager", "session-item")

    Log.info ("rescanning...")

    for si in om:iterate { type = "SiLinkable" } do
      if not checkLinkable (si, om) then
        goto skip_linkable
      end

      -- push event to find target and link
      source:call ("push-event", "select-si-virtual-and-link", si, nil)

    ::skip_linkable::
    end
  end
}:register ()

SimpleEventHook {
  name = "rescan-client-for-virtual-trigger@policy-virtual",
  interests = {
    -- on linkable added or removed, where linkable is adapter
    EventInterest {
      Constraint { "event.type", "c", "session-item-added", "session-item-removed" },
      Constraint { "event.session-item.interface", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter" },
    },
    -- on device Routes changed
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "Route" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("schedule-rescan")
  end
}:register ()
