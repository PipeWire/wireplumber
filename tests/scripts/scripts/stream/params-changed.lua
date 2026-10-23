-- Tests the 'params-changed' signal of a stream: it must be emitted when the
-- Props param of the stream changes, with the new Props objects available from
-- Stream.iterate_params(), and not when the Props param is set to the same
-- values again. Also tests that a 'stream-params-changed' event is pushed for
-- each emission of the signal.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("device-node", "Audio/Sink")

local count = 0
local event_count = 0
local started = false
local last_volume = nil

-- Returns the volume of the cached Props objects of a stream
local function getVolume (stream)
  for p in stream:iterate_params ("Props") do
    local obj = p:parse ()
    if obj.pod_type == "Object" and obj.object_id == "Props" and
        obj.properties.volume ~= nil then
      return obj.properties.volume
    end
  end
  return nil
end

-- Sets the volume to a new value, which must emit the signal once
local function setNewVolume (stream)
  count = 0
  event_count = 0
  assert (stream:set_props_param { volume = Pod.Float (0.25) })

  tu.waitUntil (function () return last_volume == 0.25 end, function ()
    Core.timeout_add (500, function ()
      Log.info ("params-changed emitted " .. tostring (count) ..
          " time(s) on a new volume")
      assert (count == 1)
      assert (event_count == 1)
      Script:finish_activation ()
      return false
    end)
  end)
end

-- Sets the volume to the value it already has, which must not emit the signal
local function setSameVolume (stream)
  count = 0
  event_count = 0
  assert (stream:set_props_param { volume = Pod.Float (0.5) })

  Core.timeout_add (500, function ()
    Log.info ("params-changed emitted " .. tostring (count) ..
        " time(s) on the same volume")
    assert (count == 0)
    assert (event_count == 0)
    setNewVolume (stream)
    return false
  end)
end

SimpleEventHook {
  name = "stream-added@test-params-changed",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()

    if started then
      return
    end
    started = true

    stream:connect ("params-changed", function (s, id)
      if id == "Props" then
        last_volume = getVolume (s)
        count = count + 1
      end
    end)

    assert (stream:set_props_param { volume = Pod.Float (0.5) })
    tu.waitUntil (function () return last_volume == 0.5 end, function ()
      -- give the remaining Props changes time to arrive
      Core.timeout_add (500, function ()
        setSameVolume (stream)
        return false
      end)
    end)
  end
}:register ()

SimpleEventHook {
  name = "stream-params-changed@test-params-changed",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-params-changed" },
      Constraint { "event.subject.param-id", "=", "Props" },
      Constraint { "node.name", "=", "device-node" },
    },
  },
  execute = function (event)
    event_count = event_count + 1
  end
}:register ()
