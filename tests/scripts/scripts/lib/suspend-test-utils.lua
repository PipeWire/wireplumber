-- WirePlumber

-- SPDX-License-Identifier: MIT

-- Common flow of the node suspend tests: a stream is linked to each device
-- node, and once all the devices are running the streams are destroyed, which
-- leaves the devices idle.

local tu = require ("test-utils")

local s = {}

s.devices = {}
-- called once, when all the devices have gone idle after running
s.on_idle = nil
-- called with the device name whenever a device suspends after that
s.on_suspended = nil

local streams_created = false
local all_ran = false
local all_idle = false

-- connects clients with the given properties, which must set
-- "session.fast-capture-start" or "session.fast-playback-start", and calls func
-- once the suspend hook has handled all of them; connecting is asynchronous
local pending_clients = 0
local on_clients_ready = nil

-- client properties asking to keep sources, sinks, or both ready
s.capture = { ["session.fast-capture-start"] = "true" }
s.playback = { ["session.fast-playback-start"] = "true" }
s.capture_and_playback = {
  ["session.fast-capture-start"] = "true",
  ["session.fast-playback-start"] = "true",
}

-- with a count of 0, func is called on the next such client-added event
function s.connectFastStartClients (count, props, func)
  pending_clients = pending_clients + math.max (count, 1)
  on_clients_ready = func
  for i = 1, count do
    tu.connectClient (props)
  end
end

SimpleEventHook {
  name = "client-added@test-suspend",
  after = "node/suspend-node-fast-start",
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
    pending_clients = pending_clients - 1
    if pending_clients == 0 and on_clients_ready then
      local func = on_clients_ready
      on_clients_ready = nil
      func ()
    end
  end
}:register ()

function s.addDevice (name, media_class, props)
  s.devices [name] = media_class
  tu.createDeviceNode (name, media_class, props)
end

function s.state (name)
  return tu.nodes [name]["state"]
end

function s.after (ms, func)
  Core.timeout_add (ms, function ()
    func ()
    return false
  end)
end

-- fail instead of waiting for the meson timeout
function s.watchdog (ms)
  s.after (ms, function ()
    assert (false, "test did not finish within " .. ms .. " ms")
  end)
end

local function allInState (state)
  for name in pairs (s.devices) do
    if tu.nodes [name] == nil or s.state (name) ~= state then
      return false
    end
  end
  return true
end

SimpleEventHook {
  name = "linkable-added@test-suspend",
  after = "linkable-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "session-item-added" },
      Constraint { "event.session-item.interface", "=", "linkable" },
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    },
  },
  execute = function (event)
    if streams_created or not tu.linkablesReady () then
      return
    end
    streams_created = true

    for name, media_class in pairs (s.devices) do
      tu.createStreamNode (
          media_class == "Audio/Sink" and "playback" or "capture", {
            ["node.name"] = "stream-" .. name,
            ["target.object"] = tu.lnkbls [name].properties ["object.serial"],
          })
    end
  end
}:register ()

SimpleEventHook {
  name = "node-state-changed@test-suspend",
  after = "node/suspend-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-state-changed" },
      Constraint { "media.class", "matches", "Audio/*" },
    },
  },
  execute = function (event)
    local node = event:get_subject ()
    local name = node.properties ["node.name"]
    local new_state = event:get_properties ()["event.subject.new-state"]

    if s.devices [name] == nil then
      return
    end
    Log.info (node, name .. " is " .. new_state)

    if not all_ran then
      if allInState ("running") then
        all_ran = true
        tu.destroyStreamNodes ()
      end
    elseif not all_idle then
      if allInState ("idle") then
        all_idle = true
        s.on_idle ()
      end
    elseif new_state == "suspended" and s.on_suspended then
      s.on_suspended (name)
    end
  end
}:register ()

return s
