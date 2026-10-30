-- Tests that the streams of two filters can link to the same filter, which
-- is not a circular link.

local tu = require ("test-utils")

Script.async_activation = true

-- creates each node once the previously created ones are ready
next_step = 1

SimpleEventHook {
  name = "stream-added@test-linking",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    if tu.streamsReady () and next_step <= #steps then
      next_step = next_step + 1
      steps [next_step - 1] ()
    end
  end
}:register ()

local function linked (a, b)
  return tu.getStreamLink (tu.streams [a], tu.streams [b]) ~= nil
end

local function createFilter (name)
  tu.createDeviceNode (name .. "-sink", "Audio/Sink", {
    ["node.link-group"] = name .. "-group",
  })
end

local function createFilterStream (name, props)
  local stream_props = {
    ["node.name"] = name .. "-stream",
    ["node.link-group"] = name .. "-group",
  }
  for k, v in pairs (props) do
    stream_props [k] = v
  end
  tu.createStreamNode ("playback", stream_props)
end

local function targetProps ()
  return {
    ["node.target"] = tu.streams ["filter-c-sink"]:get_node_bound_id (),
  }
end

steps = {
  function () createFilter ("filter-c") end,
  function () createFilterStream ("filter-c", {}) end,
  function () createFilter ("filter-a") end,
  function () createFilterStream ("filter-a", targetProps ()) end,
  function () createFilter ("filter-b") end,
  function ()
    createFilterStream ("filter-b", targetProps ())
    tu.waitUntil (function ()
      return tu.streamsReady () and
          linked ("filter-a-stream", "filter-c-sink") and
          linked ("filter-b-stream", "filter-c-sink") and
          linked ("filter-c-stream", "default-device-node")
    end, function ()
      Script:finish_activation ()
    end)
  end,
}

tu.createDeviceNode ("default-device-node", "Audio/Sink")
