-- Tests that a stream which follows the default sink, because its node
-- properties name the default sink, still moves when an explicit
-- "target.object" is set in the metadata, as pipewire-pulse does on
-- move-sink-input.

local tu = require ("test-utils")

Script.async_activation = true

started = false

tu.createDeviceNode ("default-device-node", "Audio/Sink")
tu.createDeviceNode ("defined-device-node", "Audio/Sink")

-- hook to create the stream node, once the device nodes are ready
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
    local target = event:get_data ("target")

    if started or stream:get_properties () ["node.name"] ~= "stream-node" or
        target == nil then
      return
    end
    started = true

    local default_target = tu.streams ["default-device-node"]
    local defined_target = tu.streams ["defined-device-node"]
    assert (target.properties ["node.name"] == "default-device-node")
    assert (tu.getStreamLink (stream, target) ~= nil)

    -- wait for the follow-default marker, then move the stream; a stream
    -- without a port group gets the marker in "target.node"
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
      tu.setTargetInMetadata ("target.object", "defined-device-node",
          "object.serial")
      tu.waitUntil (function ()
        return tu.getStreamLink (stream, defined_target) ~= nil and
            StreamLinkManager.get_peers_count (default_target) == 0
      end, function ()
        Script:finish_activation ()
      end)
    end)
  end
}:register ()
