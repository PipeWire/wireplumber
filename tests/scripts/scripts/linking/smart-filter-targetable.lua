-- Tests defined targets that are smart filters: a stream can link directly to a
-- targetable filter, skipping the previous ones, but a stream targeting a filter
-- that is not targetable starts from the first filter of the chain.

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

steps = {
  function ()
    createFilter ("filter-a", { ["filter.smart.before"] = "[ \"filter-b\", \"filter-c\" ]" })
  end,
  function () createFilterStream ("filter-a") end,
  function ()
    createFilter ("filter-b", {
      ["filter.smart.before"] = "[ \"filter-c\" ]",
      ["filter.smart.targetable"] = "true",
    })
  end,
  function () createFilterStream ("filter-b") end,
  function () createFilter ("filter-c", {}) end,
  function () createFilterStream ("filter-c") end,
  function ()
    tu.createStreamNode ("playback", {
      ["node.name"] = "targetable-client",
      ["node.target"] = tu.streams ["filter-b-sink"]:get_node_bound_id (),
    })
  end,
  function ()
    tu.createStreamNode ("playback", {
      ["node.name"] = "not-targetable-client",
      ["node.target"] = tu.streams ["filter-c-sink"]:get_node_bound_id (),
    })
    tu.waitUntil (function ()
      return tu.streamsReady () and
          linked ("targetable-client", "filter-b-sink") and
          linked ("not-targetable-client", "filter-a-sink")
    end, function ()
      assert (not linked ("targetable-client", "filter-a-sink"))
      assert (not linked ("not-targetable-client", "filter-c-sink"))
      Script:finish_activation ()
    end)
  end,
}

tu.createDeviceNode ("default-device-node", "Audio/Sink")
