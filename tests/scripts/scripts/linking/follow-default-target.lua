-- Tests that a stream started with "node.target" pointing at the node that is
-- also the default sink is set to follow the default, by writing -1 as the
-- target in the default metadata.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink")

-- hook to create the stream node, once the device node is ready
SimpleEventHook {
  name = "stream-added@test-linking",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local name = stream:get_properties () ["node.name"]

    if tu.streamsReady () and name ~= "stream-node" then
      local props = {
        ["node.target"] = tu.streams ["default-device-node"]:get_node_bound_id ()
      }
      tu.createStreamNode ("playback", props)
    end
  end
}:register ()

SimpleEventHook {
  name = "linking/test-linking",
  after = "linking/create-stream-link",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()

    if stream:get_properties () ["node.name"] ~= "stream-node" then
      return
    end

    local target = tu.streams ["default-device-node"]
    assert (tu.getStreamLink (stream, target) ~= nil)

    -- A stream with a port group only follows the default on its own key
    local node_id = stream:get_node_bound_id ()
    local port_group = stream:get_port_group ()

    tu.waitUntil (function ()
      if port_group == nil then
        return tu.default_metadata:find (node_id, "target.node") == "-1"
      end
      local value = tu.default_metadata:find (node_id,
          "target.stream." .. port_group)
      return value ~= nil and
          Json.Raw (value):parse () ["target.object"] == "-1"
    end, function ()
      Script:finish_activation ()
    end)
  end
}:register ()
