-- Tests that a capture stream that asks for a sink is linked to the monitor
-- ports of that sink. The monitor ports belong to the target, not to the
-- stream that asked for the link, so the sink has to end up as the output
-- stream of the link.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink")

-- hook to create the capture stream, after the device node is ready
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
      tu.createStreamNode ("capture", {
        ["target.object"] = "default-device-node",
        ["stream.capture.sink"] = "true",
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

    if stream:get_properties () ["node.name"] ~= "stream-node" then
      return
    end

    assert (target ~= nil, "the sink was not accepted as a capture target")
    assert (target.properties ["node.name"] == "default-device-node")

    local link = tu.getStreamLink (stream, target)
    assert (link ~= nil)
    assert (link:get_active_features () ~= 0)

    local out_stream, is_monitor = link:get_output_stream ()
    assert (out_stream == target, "the capture stream is the output of the link")
    assert (is_monitor, "the link is not on the monitor ports")

    Script:finish_activation ()
  end
}:register ()
