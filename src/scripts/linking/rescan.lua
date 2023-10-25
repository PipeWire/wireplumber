-- WirePlumber
--
-- Copyright © 2020-2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Handle new linkables and trigger rescanning of the graph.
-- Rescan the graph by pushing new select-target events for
-- all linkables that need to be linked
-- Cleanup links when the linkables they are associated with are removed.
-- Also, cleanup flags attached to linkables.

putils = require ("linking-utils")
cutils = require ("common-utils")
futils = require ("filter-utils")
log = Log.open_topic ("s-linking")

function checkFilter (si, om, handle_nonstreams)
  -- always handle filters if handle_nonstreams is true, even if it is disabled
  if handle_nonstreams then
    return true
  end

  -- always return true if this is not a filter
  local node = si:get_associated_proxy ("node")
  local link_group = node.properties["node.link-group"]
  if link_group == nil then
    return true
  end

  local direction = cutils.getTargetDirection (si.properties)

  -- always handle filters that are not smart
  if not futils.is_filter_smart (direction, link_group) then
    return true
  end

  -- dont handle smart filters that are disabled
  return not futils.is_filter_disabled (direction, link_group)
end

function checkLinkable (si, om, handle_nonstreams)
  local si_props = si.properties

  -- Always handle si-audio-virtual session items
  if si_props ["item.factory.name"] == "si-audio-virtual" then
    return true, si_props
  end

  -- For the rest of them, only handle stream session items
  if not si_props or (si_props ["item.node.type"] ~= "stream"
      and not handle_nonstreams) then
    return false, si_props
  end

  -- check filters
  if not checkFilter (si, om, handle_nonstreams) then
    return false, si_props
  end

  return true, si_props
end

function unhandleLinkable (si, om)
  local si_id = si.id
  local valid, si_props = checkLinkable (si, om, true)
  if not valid then
    return
  end

  log.info (si, string.format ("unhandling item: %s (%s)",
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
      log.info (silink, "... link removed")
    end
  end

  putils:clear_flags (si_id)
end

SimpleEventHook {
  name = "linking/linkable-removed",
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

    unhandleLinkable (si, om)
  end
}:register ()

function handleLinkables (source)
  local om = source:call ("get-object-manager", "session-item")

  for si in om:iterate { type = "SiLinkable" } do
    local valid, si_props = checkLinkable (si, om)
    if not valid then
      goto skip_linkable
    end

    -- check if we need to link this node at all
    local autoconnect = cutils.parseBool (si_props ["node.autoconnect"])
    if not autoconnect then
      log.debug (si, tostring (si_props ["node.name"]) .. " does not need to be autoconnected")
      goto skip_linkable
    end

    -- push event to find target and link
    source:call ("push-event", "select-target", si, nil)

    ::skip_linkable::
  end
end

SimpleEventHook {
  name = "linking/rescan",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "rescan-for-linking" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local om = source:call ("get-object-manager", "session-item")

    log:info ("rescanning...")

    -- always unlink all filters that are smart and disabled
    for si in om:iterate {
        type = "SiLinkable",
        Constraint { "node.link-group", "+" },
    } do
      local node = si:get_associated_proxy ("node")
      local link_group = node.properties["node.link-group"]
      local direction = cutils.getTargetDirection (si.properties)
      if futils.is_filter_smart (direction, link_group) and
          futils.is_filter_disabled (direction, link_group) then
        unhandleLinkable (si, om)
      end
    end

    handleLinkables (source)
  end
}:register ()

SimpleEventHook {
  name = "linking/rescan-trigger",
  interests = {
    -- on linkable added or removed, where linkable is adapter or plain node
    EventInterest {
      Constraint { "event.type", "c", "session-item-added", "session-item-removed" },
      Constraint { "event.session-item.interface", "=", "linkable" },
    },
    -- on device Routes changed
    EventInterest {
      Constraint { "event.type", "=", "device-params-changed" },
      Constraint { "event.subject.param-id", "=", "Route" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("schedule-rescan", "linking")
  end
}:register ()

SimpleEventHook {
  name = "linking/rescan-trigger-on-filters-metadata-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "metadata-changed" },
      Constraint { "metadata.name", "=", "filters" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    source:call ("schedule-rescan", "linking")
  end
}:register ()
