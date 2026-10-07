-- Tests that a stream linked to a target with unpositioned channels, such as a
-- pro audio device, keeps the channel positions of its own format instead of
-- taking the AUX channels of the target, and that its channels are linked to
-- distinct AUX channels of the target.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink", {
  ["audio.channels"] = "4",
  ["audio.position"] = "[ AUX0 AUX1 AUX2 AUX3 ]",
})

-- The audio channel of every port of the stream, keyed by the port's bound id
local function portChannels (stream)
  local channels = {}

  for port in stream:iterate_ports () do
    channels [port ["bound-id"]] = port.properties ["audio.channel"]
  end

  return channels
end

-- The target has its own AUX channels and the stream its FL and FR ones, and
-- each channel of the stream goes to a different AUX channel
local function checkLink (stream_link, stream, target)
  local target_fmt = target:get_ports_format ():parse ().properties
  assert (target_fmt.channels == 4, string.format (
      "the target has %d channels", target_fmt.channels))
  for i, channel in ipairs (target_fmt.position) do
    assert (channel == "AUX" .. (i - 1), string.format (
        "target channel %d is %s", i, channel))
  end

  local stream_fmt = stream:get_ports_format ():parse ().properties
  assert (stream_fmt.channels == 2, string.format (
      "the stream has %d channels", stream_fmt.channels))
  assert (stream_fmt.position ~= nil,
      "the stream was configured with no channel positions")
  assert (stream_fmt.position [1] == "FL" and stream_fmt.position [2] == "FR",
      string.format ("the stream channels are %s and %s",
          tostring (stream_fmt.position [1]),
          tostring (stream_fmt.position [2])))

  local out_channels = portChannels (stream)
  local in_channels = portChannels (target)
  local linked_out = {}
  local linked_in = {}
  local count = 0

  for link in stream_link:iterate_links () do
    local props = link.properties
    local out_channel = out_channels [tonumber (props ["link.output.port"])]
    local in_channel = in_channels [tonumber (props ["link.input.port"])]

    assert (out_channel == "FL" or out_channel == "FR", string.format (
        "linked output channel %s", tostring (out_channel)))
    assert (in_channel ~= nil and in_channel:sub (1, 3) == "AUX",
        string.format ("linked input channel %s", tostring (in_channel)))
    assert (not linked_out [out_channel], out_channel .. " linked twice")
    assert (not linked_in [in_channel], in_channel .. " linked twice")

    linked_out [out_channel] = true
    linked_in [in_channel] = true
    count = count + 1
  end

  assert (count == 2, "expected 2 links, found " .. tostring (count))
end

-- hook to create the stream node, after the device node is ready
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
    local stream_link = tu.getStreamLink (stream, target)
    assert (stream_link ~= nil)

    checkLink (stream_link, stream, target)

    Script:finish_activation ()
  end
}:register ()
