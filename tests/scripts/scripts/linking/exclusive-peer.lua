-- Tests that a stream is not linked to a target that another stream already
-- holds exclusively. node.exclusive belongs to the stream that asked for the
-- link, so it has to be looked up on the target's existing peers.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink")

-- hook to create the exclusive stream, after the device node is ready
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

    if tu.streamsReady () and name == "default-device-node" then
      tu.createStreamNode ("playback", {
        ["node.name"] = "exclusive-stream",
        ["node.exclusive"] = "true",
      })
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
    local name = stream:get_properties () ["node.name"]

    if name == "exclusive-stream" then
      if target == nil then
        return
      end

      local link = tu.getStreamLink (stream, target)
      assert (link ~= nil)
      assert (target.properties ["node.name"] == "default-device-node")
      assert (link:get_active_features () ~= 0)
      assert (StreamLinkManager.get_peers_count (target) == 1)

      -- create the second stream only now, so that the sink is already held
      -- exclusively when this one is prepared
      tu.createStreamNode ("playback", { ["node.name"] = "second-stream" })
    elseif name == "second-stream" then
      assert (target == nil, "linked to a target that is held exclusively")
      assert (StreamLinkManager.get_peers_count (stream) == 0)

      Script:finish_activation ()
    end
  end
}:register ()
