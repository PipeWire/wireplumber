-- WirePlumber
--
-- Copyright © 2025 Pauli Virtanen
--    @author Pauli Virtanen <pav@iki.fi>
--
-- SPDX-License-Identifier: MIT
--
-- Pause media player applications when (one of) their output
-- target(s) goes away.

cutils = require ("common-utils")
lutils = require ("linking-utils")

log = Log.open_topic ("s-linking.mpris")

mpris = Plugin.find("mpris")

RESCAN_DELAY_MSEC = 1000

-- Delaying rescan while pausing players
pending_ops = 0
need_rescan = false

-- Links between nodes: links_in [in_node_id] = { out_node_id, ... }
links_in = {}
links_initialized = false

-- Link nodes: link_nodes [link.id] = { in_node_id, out_node_id }
link_nodes = {}

-- Get nodes that are (indirectly) linked to `si` by link group
-- or links with input direction. Returns table { [node_id] = node, ... }
function getLinkedNodes (start_id)
  local node_om = cutils.get_object_manager ("node")
  local groups = {}
  local link_groups = {}

  log:trace(string.format("start %d", start_id))

  -- construct groups based on node.link-group
  for node in node_om:iterate () do
    local id = node ["bound-id"]

    if groups [id] ~= nil then
      return
    end

    local link_group = node.properties ["node.link-group"]
    local group = nil

    -- join via link groups
    if link_group ~= nil then
      group = groups [link_groups [link_group]]
      link_groups [link_group] = id
    end
    if group == nil then
      group = {}
    end

    group [id] = node
    groups [id] = group
  end

  -- follow links
  local group = {}
  local active = { [start_id] = true }

  while true do
    local a_id = next(active)
    if a_id == nil then
      break
    end

    active [a_id] = nil

    local b_ids = links_in [a_id]
    if b_ids ~= nil then
      for b_id in pairs (b_ids) do
        if group [b_id] == nil and groups [b_id] ~= nil then
          for k, v in pairs (groups [b_id]) do
            if group [k] == nil then
              active [k] = true
              group [k] = v
              log:trace(string.format("node %d", k))
            end
          end
        end
      end
    end
  end

  return group
end

-- Track link status.
--
-- We need to know what state links were in just before a sink node is
-- removed. We rely here on the following facts:
--
-- * When node is removed existing links to it are removed by server synchronously
-- * Although it appears server notifies link removal *after* the node, use Core sync
--   to delay handling link removal after node removal, to be sure
-- * event_priority(node-removed, session-item-removed) >  event_priority(link-removed)
--   then ensures we first handle item removal, then update links

function updateLink (in_id, out_id, remove)
  if links_in [in_id] == nil then
    links_in [in_id] = {}
  end
  if remove then
    links_in [in_id] [out_id] = nil
  else
    links_in [in_id] [out_id] = true
  end
end

function updateLinks (links_om)
  for link in links_om:iterate () do
    local lprops = link.properties
    in_id = tonumber (lprops ["link.input.node"])
    out_id = tonumber (lprops ["link.output.node"])
    updateLink (in_id, out_id, false)
  end
end

function initializeLinks ()
  local links_om = ObjectManager {
    Interest { type = "link" }
  }
  links_om:connect ("installed", updateLinks)
end

SimpleEventHook {
  name = "linking/mpris-pause@track-links",
  interests = {
    EventInterest {
      Constraint { "event.type", "c", "link-added", "link-removed" },
    },
  },
  execute = function (event)
    local link = event:get_subject ()
    local eprops = event:get_properties ()

    local in_id
    local out_id
    local remove = false

    if eprops ["event.type"] == "link-added" then
      local lprops = link.properties
      in_id = tonumber (lprops ["link.input.node"])
      out_id = tonumber (lprops ["link.output.node"])
      link_nodes [link.id] = { in_id, out_id }
    elseif link_nodes [link.id] ~= nil then
      in_id = link_nodes [link.id] [1]
      out_id = link_nodes [link.id] [2]
      remove = true
    end

    if in_id == nil or out_id == nil then
      return
    end

    Core.sync(function()
        updateLink (in_id, out_id, remove)
    end)
  end
}:register ()

initializeLinks ()

-- Pause media applications associated with the streams linked to a sink to be removed
SimpleEventHook {
  name = "linking/mpris-pause",
  before = "linking/linkable-removed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "session-item-removed" },
      Constraint { "item.node.type", "=", "device" },
      Constraint { "item.node.direction", "=", "input" },
    },
  },
  execute = function (event)
    if not Settings.get_boolean ("linking.pause-playback") then
      return
    end

    local players = mpris:call ("get-players")
    if next(players) == nil then
      return
    end

    -- find clients to handle
    local si = event:get_subject ()
    local source = event:get_source ()
    local client_om = source:call ("get-object-manager", "client")
    local node = si:get_associated_proxy ("node")
    local node_group = getLinkedNodes (tonumber (si.properties ["node.id"]))
    local client_ids = {}

    for id, node in pairs (node_group) do
      local media_class = tostring(node.properties ["media.class"])
      if media_class:find ("^Stream/Output") then
        client_ids [node.properties ["client.id"]] = true
      end
    end

    -- find players to handle
    local pause_players = {}

    for client_id in pairs (client_ids) do
      local client = client_om:lookup {
        Constraint { "bound-id", "=", client_id, type = "gobject" }
      }
      if not client then
        goto next
      end

      log:debug (si, string.format ("node %s removed: was linked to client %s (%s pid %s)",
          tostring(si.properties ["node.id"]),
          tostring(client.properties ["application.name"]),
          tostring(client.properties ["application.id"]),
          tostring(client.properties ["application.process.id"])))

      for _, player in ipairs(players) do
        local match = false

        if client.properties ["pipewire.access.portal.app_id"] == nil and player ["flatpak-app-id"] == nil then
          -- assume non-flatpak apps serve audio from same or child process as MPRIS
          if player ["pid"] ~= nil then
            local keys = { "pipewire.sec.pid", "application.process.id" }
            for _, key in ipairs(keys) do
              local pid = tonumber (client.properties [key])
              if pid ~= nil and mpris:call ("match-pid", player ["pid"], pid) then
                log:debug (si, string.format(".. match %s %u ~ %u", key, pid, player ["pid"]))
                match = true
              end
            end
          end
        elseif client.properties ["pipewire.access.portal.app_id"] == player ["flatpak-app-id"] then
          local instance_id = client.properties ["pipewire.access.portal.instance_id"]
          if instance_id == nil then
            -- Only new Pipewire versions have instance_id
            log:debug (si, string.format(".. match pipewire.access.portal.app_id = %s", player ["flatpak-app-id"]))
            match = true
          elseif instance_id == player ["flatpak-instance-id"] then
            log:debug (si, string.format(".. match pipewire.access.portal.app_id = %s instance_id = %s",
                player ["flatpak-app-id"], player ["flatpak-instance-id"]))
            match = true
          end
        end

        if match then
          pause_players [player ["name"]] = true
        end
      end

      ::next::
    end

    -- handle players
    for bus_name in pairs(pause_players) do
      log:info (si, string.format("node %s removed: pausing linked media player %s",
          tostring(si.properties ["node.id"]),
          bus_name))

      local op = mpris:call ("pause", bus_name)
      if op ["result"] == 0 then
        log:debug ("pause pending")
        pending_ops = pending_ops + 1
        op:connect("notify::result", function (op, pspec)
            if pending_ops > 0 then
              pending_ops = pending_ops - 1
            end
            log:debug (string.format("pause completed, res = %d, %d remaining",
                op ["result"], pending_ops))
            if pending_ops == 0 and need_rescan then
              -- Some players respond to DBus before actually pausing output,
              -- so add also a small delay
              Core.timeout_add(RESCAN_DELAY_MSEC, function()
                  log:debug ("continue rescan")
                  source:call ("push-event", "rescan-for-linking", nil, nil)
              end)
            end
        end)
      end
    end
  end
}:register ()

-- Do not perform rescans while we are pausing media players
SimpleEventHook {
  name = "linking/mpris-pause-disable-rescan",
  before = "linking/rescan",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "rescan-for-linking" },
    },
  },
  execute = function (event)
    if pending_ops > 0 then
      log:info ("rescan disabled, wait for pending operations")
      need_rescan = true
      event:stop_processing ()
    else
      need_rescan = false
    end
  end
}:register ()
