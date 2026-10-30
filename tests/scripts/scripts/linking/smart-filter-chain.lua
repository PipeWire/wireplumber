-- Tests that smart filters are chained in the order set by filter.smart.before,
-- no matter which one is created first, and that a client stream goes through
-- all of them.

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

local function createFilter (name, props)
  local sink_props = {
    ["node.link-group"] = name .. "-group",
    ["filter.smart"] = "true",
    ["filter.smart.name"] = name,
  }
  for k, v in pairs (props) do
    sink_props [k] = v
  end
  tu.createDeviceNode (name .. "-sink", "Audio/Sink", sink_props)
end

local function createFilterStream (name)
  tu.createStreamNode ("playback", {
    ["node.name"] = name .. "-stream",
    ["node.link-group"] = name .. "-group",
  })
end

-- each filter is created after the one it has to go before
steps = {
  function () createFilter ("filter-c", {}) end,
  function () createFilterStream ("filter-c") end,
  function ()
    createFilter ("filter-b", { ["filter.smart.before"] = "[ \"filter-c\" ]" })
  end,
  function () createFilterStream ("filter-b") end,
  function ()
    createFilter ("filter-a", { ["filter.smart.before"] = "[ \"filter-b\" ]" })
  end,
  function () createFilterStream ("filter-a") end,
  function ()
    tu.createStreamNode ("playback", { ["node.name"] = "client-stream" })
    tu.waitUntil (function ()
      return tu.streamsReady () and
          linked ("client-stream", "filter-a-sink") and
          linked ("filter-a-stream", "filter-b-sink") and
          linked ("filter-b-stream", "filter-c-sink") and
          linked ("filter-c-stream", "default-device-node")
    end, function ()
      -- the chain has to stay the same on further rescans
      Plugin.find ("standard-event-source"):call ("schedule-rescan", "linking")
      Core.timeout_add (100, function ()
        assert (linked ("filter-a-stream", "filter-b-sink"))
        assert (linked ("filter-b-stream", "filter-c-sink"))
        assert (not linked ("filter-b-stream", "default-device-node"))
        Script:finish_activation ()
        return false
      end)
    end)
  end,
}

tu.createDeviceNode ("default-device-node", "Audio/Sink")
