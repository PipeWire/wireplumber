-- Tests that a defined target of an incompatible media type is rejected.
-- can_link() accepts it, so is_compatible() has to be the one that rejects it;
-- otherwise create_link() returns nil and the linking hook fails.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Source")

-- hook to create the stream nodes, after the device nodes are ready. The video
-- stream goes first, so that it exists when the audio stream asks for it
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

    if not tu.streamsReady () then
      return
    end

    if name == "default-device-node" then
      tu.createStreamNode ("video-playback", {
        ["node.name"] = "video-stream-node",
      })
    elseif name == "video-stream-node" then
      tu.createStreamNode ("capture", {
        ["target.object"] = "video-stream-node",
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
    local stream_props = stream:get_properties ()

    if target == nil or stream_props ["node.name"] ~= "stream-node" then
      return
    end

    local link = tu.getStreamLink (stream, target)
    assert (link ~= nil)
    assert (target.properties ["node.name"] == "default-device-node")
    assert (link:get_active_features () ~= 0)

    Script:finish_activation ()
  end
}:register ()
