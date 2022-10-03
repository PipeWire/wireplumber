-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script registers hooks needed to perform policy rescan.

-- settings file: policy.conf

local putils = require ("policy-utils")
local cutils = require ("common-utils")

local defaults = {}
defaults.move = true

local config = {}
config.move = Settings.parse_boolean_safe (
    "policy.default.move", defaults.move)

function settingsChangedCallback (_, setting, _)
  config.move = Settings.parse_boolean_safe ("policy.default.move", config.move)
end

Settings.subscribe ("policy.default.move", settingsChangedCallback)

function parseBool (var)
  return cutils.parseBool (var)
end

-- check if target node is defined explicitly.
-- This defination can be done in two ways.
-- 1. "node.target"/"target.object" in the node properties
-- 2. "target.node"/"target.object" in default metadata
function findDefinedTarget (event)
  local si = event:get_subject ()
  local si_props = si.properties
  local si_id = si.id;
  local si_flags = putils.get_flags (si_id)
  local si_target = nil

  Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
    tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

  local metadata = config.move and putils.get_default_metadata_object ()
  local target_key
  local target_value = nil
  local node_defined = false
  local target_picked = nil

  si_flags.node_name = si_props ["node.name"]
  si_flags.node_id = si_props ["node.id"]

  if si_props ["target.object"] ~= nil then
    target_value = si_props ["target.object"]
    target_key = "object.serial"
    node_defined = true
  elseif si_props ["node.target"] ~= nil then
    target_value = si_props ["node.target"]
    target_key = "node.id"
    node_defined = true
  end

  if metadata then
    local id = metadata:find (si_props ["node.id"], "target.object")
    if id ~= nil then
      target_value = id
      target_key = "object.serial"
      node_defined = false
    else
      id = metadata:find (si_props ["node.id"], "target.node")
      if id ~= nil then
        target_value = id
        target_key = "node.id"
        node_defined = false
      end
    end
  end

  if target_value == "-1" then
    target_picked = false
    si_target = nil
  elseif target_value and tonumber (target_value) then
    si_target = linkables_om:lookup {
      Constraint { target_key, "=", target_value },
    }
    if si_target and putils.canLink (si_props, si_target) then
      target_picked = true
    end
  elseif target_value then
    for lnkbl in linkables_om:iterate () do
      local target_props = lnkbl.properties
      if (target_props ["node.name"] == target_value or
          target_props ["object.path"] == target_value) and
          target_props ["item.node.direction"] == cutils.getTargetDirection (si_props) and
          putils.canLink (si_props, lnkbl) then
        target_picked = true
        si_target = lnkbl
        break
      end

    end
  end

  local can_passthrough, passthrough_compatible
  if si_target then
    passthrough_compatible, can_passthrough =
    putils.checkPassthroughCompatibility (si, si_target)

    if not passthrough_compatible then
      si_target = nil
    end
  end

  -- if the client has seen a target that we haven't yet prepared, stop the
  -- event and wait(for one time) for next rescan to happen and hope for the
  -- best.

  if target_picked
      and not si_target
      and not si_flags.was_handled
      and not si_flags.done_waiting then
    Log.info(si, "... waiting for target")
    si_flags.done_waiting = true
    event:stop_processing ()

  elseif target_picked then
    Log.info (si,
      string.format ("... defined target picked: %s (%s), can_passthrough:%s",
        tostring (si_target.properties ["node.name"]),
        tostring (si_target.properties ["node.id"]),
        tostring (can_passthrough)))
    si_flags.si_target = si_target
    si_flags.has_node_defined_target = node_defined
    si_flags.can_passthrough = can_passthrough
  else
    si_flags.si_target = nil
    si_flags.can_passthrough = nil
    si_flags.has_node_defined_target = nil
  end

  putils.set_flags (si_id, si_flags)
end

-- check if default nodes can be picked up as target node.
function findDefaultTarget (event)
  local si = event:get_subject ()
  local si_id = si.id
  local si_flags = putils.get_flags (si_id)
  local si_target = si_flags.si_target

  if si_target or (default_nodes == nil) then
    -- bypass the hook as the target is already picked up.
    return
  end

  local si_props = si.properties
  local target_picked = false

  Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
    tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

  local si_target = putils.findDefaultLinkable (si)

  local can_passthrough, passthrough_compatible
  if si_target then
    passthrough_compatible, can_passthrough =
    putils.checkPassthroughCompatibility (si, si_target)
    if putils.canLink (si_props, si_target) and passthrough_compatible then
      target_picked = true;
    end
  end

  if target_picked then
    Log.info (si,
      string.format ("... default target picked: %s (%s), can_passthrough:%s",
        tostring (si_target.properties ["node.name"]),
        tostring (si_target.properties ["node.id"]),
        tostring (can_passthrough)))
    si_flags.si_target = si_target
    si_flags.can_passthrough = can_passthrough
  else
    si_flags.si_target = nil
    si_flags.can_passthrough = nil
  end

  putils.set_flags (si_id, si_flags)
end

-- function sampleUserPolicyHook(event)
--   local si = event:get_subject()
--   local si_id = si.id
--   -- pick up the flags
--   local si_flags = putils.get_flags(si_id)
--   local si_target = si_flags.si_target

--   if si_target then
--     -- one can choose to bypass the hook if the target is picked.
--     return
--   end

--   local si_props = si.properties

--   Log.info(si, string.format("handling item: %s (%s) si id(%s)",
--     tostring(si_props["node.name"]), tostring(si_props["node.id"]), si_id))

--   -- implement logic to pick target

--   -- save back the flags.
--   putils.set_flags(si_id, si_flags)
-- end

-- Traverse through all the possible targets to pick up target node.
function findBestTarget (event)
  local si = event:get_subject ()
  local si_id = si.id
  local si_flags = putils.get_flags (si_id)
  local si_target = si_flags.si_target

  if si_target then
    -- bypass the hook as the target is already picked up.
    return
  end

  local si_props = si.properties
  local target_direction = cutils.getTargetDirection (si_props)
  local target_picked = nil
  local target_can_passthrough = false
  local target_priority = 0
  local target_plugged = 0

  Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
    tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

  for si_target in linkables_om:iterate {
    Constraint { "item.node.type", "=", "device" },
    Constraint { "item.node.direction", "=", target_direction },
    Constraint { "media.type", "=", si_props ["media.type"] },
  } do
    local si_target_props = si_target.properties
    local si_target_node_id = si_target_props ["node.id"]
    local priority = tonumber (si_target_props ["priority.session"]) or 0

    Log.debug (string.format ("Looking at: %s (%s)",
      tostring (si_target_props ["node.name"]),
      tostring (si_target_node_id)))

    if not putils.canLink (si_props, si_target) then
      Log.debug ("... cannot link, skip linkable")
      goto skip_linkable
    end

    if not putils.haveAvailableRoutes (si_target_props) then
      Log.debug ("... does not have routes, skip linkable")
      goto skip_linkable
    end

    local passthrough_compatible, can_passthrough =
    putils.checkPassthroughCompatibility (si, si_target)
    if not passthrough_compatible then
      Log.debug ("... passthrough is not compatible, skip linkable")
      goto skip_linkable
    end

    local plugged = tonumber (si_target_props ["item.plugged.usec"]) or 0

    Log.debug ("... priority:" .. tostring (priority) .. ", plugged:" .. tostring (plugged))

    -- (target_picked == NULL) --> make sure atleast one target is picked.
    -- (priority > target_priority) --> pick the highest priority linkable(node)
    -- target.
    -- (priority == target_priority and plugged > target_plugged) --> pick the
    -- latest connected/plugged(in time) linkable(node) target.
    if (target_picked == nil or
        priority > target_priority or
        (priority == target_priority and plugged > target_plugged)) then
      Log.debug ("... picked")
      target_picked = si_target
      target_can_passthrough = can_passthrough
      target_priority = priority
      target_plugged = plugged
    end
    ::skip_linkable::
  end

  if target_picked then
    Log.info (si,
      string.format ("... best target picked: %s (%s), can_passthrough:%s",
        tostring (target_picked.properties ["node.name"]),
        tostring (target_picked.properties ["node.id"]),
        tostring (target_can_passthrough)))
    si_flags.si_target = target_picked
    si_flags.can_passthrough = target_can_passthrough
  else
    si_flags.si_target = nil
    si_flags.can_passthrough = nil
  end

  putils.set_flags (si_id, si_flags)
end

-- remove the existing link if needed, check the properties of target, which
-- indicate it is not available for linking. If no target is available, send
-- down an error to the corresponding client.
function prepareLink (event)
  local si = event:get_subject ()
  local si_id = si.id
  local si_flags = putils.get_flags (si_id)
  local si_target = si_flags.si_target
  local si_props = si.properties

  local reconnect = not parseBool (si_props ["node.dont-reconnect"])
  local exclusive = parseBool (si_props ["node.exclusive"])
  local si_must_passthrough = parseBool (si_props ["item.node.encoded-only"])

  Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
    tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))


  -- Check if item is linked to proper target, otherwise re-link
  if si_flags.peer_id then
    if si_target and si_flags.peer_id == si_target.id then
      Log.debug (si, "... already linked to proper target")
      -- Check this also here, in case in default targets changed
      putils.checkFollowDefault (si, si_target,
        si_flags.has_node_defined_target)
      si_target = nil
      goto done
    end

    local link = putils.lookupLink (si_id, si_flags.peer_id)
    if reconnect then
      if link ~= nil then
        -- remove old link
        if ((link:get_active_features () & Feature.SessionItem.ACTIVE) == 0)
        then
          -- remove also not yet activated links: they might never become
          -- active, and we need not wait for it to become active
          Log.warning (link, "Link was not activated before removing")
        end
        si_flags.peer_id = nil
        link:remove ()
        Log.info (si, "... moving to new target")
      end
    else
      if link ~= nil then
        Log.info (si, "... dont-reconnect, not moving")
        goto done
      end
    end
  end

  -- if the stream has dont-reconnect and was already linked before,
  -- don't link it to a new target
  if not reconnect and si_flags.was_handled then
    si_target = nil
    goto done
  end

  -- check target's availability
  if si_target then
    local target_is_linked, target_is_exclusive = putils.isLinked (si_target)
    if target_is_exclusive then
      Log.info (si, "... target is linked exclusively")
      si_target = nil
    end

    if target_is_linked then
      if exclusive or si_must_passthrough then
        Log.info (si, "... target is already linked, cannot link exclusively")
        si_target = nil
      else
        -- disable passthrough, we can live without it
        si_flags.can_passthrough = false
      end
    end
  end

  if not si_target then
    Log.info (si, "... target not found, reconnect:" .. tostring (reconnect))

    local node = si:get_associated_proxy ("node")
    if not reconnect then
      Log.info (si, "... destroy node")
      node:request_destroy ()
    elseif si_flags.was_handled then
      Log.info (si, "... waiting reconnect")
      return
    end

    local client_id = node.properties ["client.id"]
    if client_id then
      local client = clients_om:lookup {
        Constraint { "bound-id", "=", client_id, type = "gobject" }
      }
      if client then
        client:send_error (node ["bound-id"], -2, "no node available")
      end
    end
  end

  ::done::
  si_flags.si_target = si_target
  putils.set_flags (si_id, si_flags)
end

function createLink (event)
  local si = event:get_subject ()
  local si_id = si.id
  local si_flags = putils.get_flags (si_id)
  local si_target = si_flags.si_target

  if not si_target then
    -- bypass the hook, nothing to link to.
    return
  end

  local si_props = si.properties
  local target_props = si_target.properties
  local out_item = nil
  local in_item = nil
  local si_link = nil
  local passthrough = si_flags.can_passthrough

  Log.info (si, string.format ("handling item: %s (%s) si id(%s)",
    tostring (si_props ["node.name"]), tostring (si_props ["node.id"]), si_id))

  local exclusive = parseBool (si_props ["node.exclusive"])
  local passive = parseBool (si_props ["node.passive"]) or
      parseBool (target_props ["node.passive"])

  -- break rescan if tried more than 5 times with same target
  if si_flags.failed_peer_id ~= nil and
      si_flags.failed_peer_id == si_target.id and
      si_flags.failed_count ~= nil and
      si_flags.failed_count > 5 then
    Log.warning (si, "tried to link on last rescan, not retrying")
    goto done
  end

  if si_props ["item.node.direction"] == "output" then
    -- playback
    out_item = si
    in_item = si_target
  else
    -- capture
    in_item = si
    out_item = si_target
  end

  Log.info (si,
    string.format ("link %s <-> %s passive:%s, passthrough:%s, exclusive:%s",
      tostring (si_props ["node.name"]),
      tostring (target_props ["node.name"]),
      tostring (passive), tostring (passthrough), tostring (exclusive)))

  -- create and configure link
  si_link = SessionItem ("si-standard-link")
  if not si_link:configure {
    ["out.item"] = out_item,
    ["in.item"] = in_item,
    ["passive"] = passive,
    ["passthrough"] = passthrough,
    ["exclusive"] = exclusive,
    ["out.item.port.context"] = "output",
    ["in.item.port.context"] = "input",
    ["is.policy.item.link"] = true,
  } then
    Log.warning (si_link, "failed to configure si-standard-link")
    goto done
  end

  -- register
  si_flags.peer_id = si_target.id
  si_flags.failed_peer_id = si_target.id
  if si_flags.failed_count ~= nil then
    si_flags.failed_count = si_flags.failed_count + 1
  else
    si_flags.failed_count = 1
  end
  si_link:register ()

  -- activate
  si_link:activate (Feature.SessionItem.ACTIVE, function (l, e)
    if e then
      Log.info (l, "failed to activate si-standard-link: " .. tostring (si) .. " error:" .. tostring (e))
      if si_flags ~= nil then
        si_flags.peer_id = nil
      end
      l:remove ()
    else
      if si_flags ~= nil then
        si_flags.failed_peer_id = nil
        if si_flags.peer_id == nil then
          si_flags.peer_id = si_target.id
        end
        si_flags.failed_count = 0
      end
      Log.info (l, "activated si-standard-link " .. tostring (si))
    end
    putils.set_flags (si_id, si_flags)
  end)

  ::done::
  si_flags.was_handled = true
  putils.set_flags (si_id, si_flags)
  putils.checkFollowDefault (si, si_target, si_flags.has_node_defined_target)
end

linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    -- only handle si-audio-adapter and si-node
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "active-features", "!", 0, type = "gobject" },
  }
}

linkables_om:activate ()

clients_om = ObjectManager { Interest { type = "client" } }

clients_om:activate ()

default_nodes = Plugin.find ("default-nodes-api")

SimpleEventHook {
  name = "link-target@policy-hooks",
  type = "after-events-with-event",
  priority = "link-target-si",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "find-target-si-and-link" },
    },
  },
  execute = function (event)
    createLink (event)
  end
}:register ()

SimpleEventHook {
  name = "prepare-link@policy-hooks",
  type = "after-events-with-event",
  priority = "prepare-link-si",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "find-target-si-and-link" },
    },
  },
  execute = function (event)
    prepareLink (event)
  end
}:register ()

SimpleEventHook {
  name = "find-best-target@policy-hooks",
  type = "after-events-with-event",
  priority = "find-best-target-si",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "find-target-si-and-link" },
    },
  },
  execute = function (event)
    findBestTarget (event)
  end
}:register ()

SimpleEventHook {
  name = "find-default-target@policy-hooks",
  type = "after-events-with-event",
  priority = "find-default-target-si",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "find-target-si-and-link" },
    },
  },
  execute = function (event)
    findDefaultTarget (event)
  end
}:register ()

-- an example of an user injectible hook, uncomment to see it in action.
-- SimpleEventHook { name = "sample-user-hook@policy-hooks", type =
--   "after-events-with-event", priority = "sample-user-policy-hook",

--   interests = {
--     EventInterest {
--       Constraint { "event.type", "=", "find-target-si-and-link" },
--     },
--   },
--   execute = function(event)
--     sampleUserPolicyHook (event)
--   end
-- }:register()

SimpleEventHook {
  name = "find-defined-target@policy-hooks",
  type = "after-events-with-event",
  priority = "find-defined-target-si",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "find-target-si-and-link" },
    },
  },
  execute = function (event)
    findDefinedTarget (event)
  end
}:register ()
