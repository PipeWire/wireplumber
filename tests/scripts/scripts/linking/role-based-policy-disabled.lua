-- Tests that a stream is linked normally to a target marked with
-- policy.role-based.target when the role-based policy is not loaded.
-- Activating such a link is the job of linking/handle-media-role-links, so
-- without that script nothing would ever activate it.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink", {
  ["policy.role-based.target"] = "true",
})

-- hook to create the stream, after the device node is ready
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
      tu.createStreamNode ("playback")
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

    if stream:get_properties () ["node.name"] ~= "stream-node" then
      return
    end

    assert (target ~= nil)
    assert (target.properties ["node.name"] == "default-device-node")

    local link = tu.getStreamLink (stream, target)
    assert (link ~= nil)
    assert (link:get_active_features () ~= 0, "link was left inactive")

    Script:finish_activation ()
  end
}:register ()
