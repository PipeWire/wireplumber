-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
log = Log.open_topic ("s-node")

sources = {}

-- Some devices take long to resume from suspend, which delays the start of a
-- stream; such nodes can set "node.slow-resume" to "true". A client that must
-- start streams immediately can ask for these nodes to stay idle instead of
-- suspending by setting "session.fast-capture-start" (for sources) or
-- "session.fast-playback-start" (for sinks) to "true"; the request lasts until
-- the client disconnects.

-- idle slow-resume nodes left unsuspended for such a client, by bound id;
-- each entry holds the node and its properties
held = {}
-- suspend timeout of each idle node, by bound id
timeouts = {}

function isSlowResume (props)
  return cutils.parseBool (props["node.slow-resume"])
end

-- whether a node is a source, a sink, or both, going by its media class
function directions (props)
  local class = props["media.class"] or ""
  local duplex = class:find ("Duplex") ~= nil
  return duplex or class:find ("Source") ~= nil,
      duplex or class:find ("Sink") ~= nil
end

function fastStartRequested (source, props)
  if not isSlowResume (props) then
    return false
  end

  local is_source, is_sink = directions (props)
  for client in source:call ("get-object-manager", "client"):iterate () do
    local cprops = client.properties
    if (is_source and cutils.parseBool (cprops["session.fast-capture-start"])) or
        (is_sink and cutils.parseBool (cprops["session.fast-playback-start"])) then
      return true
    end
  end
  return false
end

-- held nodes get no further state change once removed; drop them
function forgetRemovedNodes ()
  for id, entry in pairs (held) do
    if (entry.node:get_active_features() & Feature.Proxy.BOUND) == 0 then
      held[id] = nil
      timeouts[id] = nil
    end
  end
end

function cancelSuspend (id)
  if sources[id] then
    sources[id]:destroy()
    sources[id] = nil
  end
end

function scheduleSuspend (node, timeout)
  local id = node["bound-id"]

  if timeout == 0 then
    return
  end

  -- add idle timeout; timeout_add() expects whole ms
  sources[id] = Core.timeout_add(math.floor(timeout * 1000 + 0.5), function()
    -- Suspend the node
    -- but check first if the node still exists
    if (node:get_active_features() & Feature.Proxy.BOUND) ~= 0 then
      log:info(node, "was idle for a while; suspending ...")
      node:send_command("Suspend")
    end

    -- Unref the source
    sources[id] = nil

    -- false (== G_SOURCE_REMOVE) destroys the source so that this
    -- function does not get fired again after 5 seconds
    return false
  end)
end

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
    -- the node's properties, as they were when the event was created
    local props = event:get_properties ()
    local new_state = props["event.subject.new-state"]

    log:debug (node, "changed state to " .. new_state)

    -- the node may be gone by the time the event is dispatched; its suspend
    -- source, if any, finds that out when it fires
    if (node:get_active_features() & Feature.Proxy.BOUND) == 0 then
      return
    end

    forgetRemovedNodes ()

    -- Always clear the current source if any
    local id = node["bound-id"]
    cancelSuspend (id)
    held[id] = nil
    timeouts[id] = nil

    -- Add a timeout source if idle for at least 5 seconds
    if new_state == "idle" or new_state == "error" then
      -- honor "session.suspend-timeout-seconds" if specified
      local timeout = tonumber(props["session.suspend-timeout-seconds"]) or 5
      timeouts[id] = timeout

      if new_state == "idle" and
          fastStartRequested (event:get_source (), props) then
        log:debug (node, "idle; kept ready for a client")
        held[id] = { node = node, props = props }
      else
        scheduleSuspend (node, timeout)
      end
    end
  end
}:register ()

SimpleEventHook {
  name = "node/suspend-node-fast-start",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "client-added" },
      Constraint { "session.fast-capture-start", "+", type = "pw" },
    },
    EventInterest {
      Constraint { "event.type", "=", "client-added" },
      Constraint { "session.fast-playback-start", "+", type = "pw" },
    },
  },
  execute = function (event)
    log:info (event:get_subject (), "keeping slow-resume nodes ready")
    forgetRemovedNodes ()

    -- nodes that went idle earlier still have a suspend pending
    local source = event:get_source ()
    for node in source:call ("get-object-manager", "node"):iterate () do
      local id = node["bound-id"]
      local props = node.properties
      if sources[id] and node["state"] == "idle" and props and
          fastStartRequested (source, props) then
        cancelSuspend (id)
        held[id] = { node = node, props = props }
      end
    end
  end
}:register ()

SimpleEventHook {
  name = "node/suspend-node-fast-start-release",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "client-removed" },
      Constraint { "session.fast-capture-start", "+", type = "pw" },
    },
    EventInterest {
      Constraint { "event.type", "=", "client-removed" },
      Constraint { "session.fast-playback-start", "+", type = "pw" },
    },
  },
  execute = function (event)
    local source = event:get_source ()

    -- held nodes get no further state change; suspend those that no
    -- remaining client asks to keep ready on their normal timeout (the
    -- removed client is no longer listed)
    for id, entry in pairs (held) do
      if not fastStartRequested (source, entry.props) then
        held[id] = nil
        if (entry.node:get_active_features() & Feature.Proxy.BOUND) ~= 0 then
          scheduleSuspend (entry.node, timeouts[id])
        end
      end
    end
  end
}:register ()
