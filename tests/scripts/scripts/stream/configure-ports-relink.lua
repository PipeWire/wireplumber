-- Tests that the link between two streams survives a settings change that
-- reconfigures the ports of both of them: the two streams end up in the same
-- port configuration and the links join the matching channels again.

local tu = require ("test-utils")

Script.async_activation = true

reconfigured = false

tu.createDeviceNode ("default-device-node", "Audio/Sink")

-- The audio channel of every port of the stream, keyed by the port's bound id.
-- A port that carries no channel, such as a control port, is named instead.
local function portChannels (stream)
  local channels = {}

  for port in stream:iterate_ports () do
    channels [port ["bound-id"]] =
        port.properties ["audio.channel"] or port.properties ["port.name"]
  end

  return channels
end

-- Whether the stream has a port with this channel, or with this name when the
-- port carries no channel
local function hasPort (stream, name)
  for _, channel in pairs (portChannels (stream)) do
    if channel == name then
      return true
    end
  end

  return false
end

-- Both streams have to end up in the same DSP format, channel positions
-- included. Configuring a stream from its own properties instead gives it the
-- format it prefers, and a client stream prefers one with no positions at all.
local function checkSameFormat (out_stream, in_stream)
  local out_fmt = out_stream:get_ports_format ():parse ().properties
  local in_fmt = in_stream:get_ports_format ():parse ().properties

  assert (out_fmt.format == in_fmt.format, string.format (
      "formats differ: %s and %s", out_fmt.format, in_fmt.format))
  assert (out_fmt.rate == in_fmt.rate, string.format (
      "rates differ: %d and %d", out_fmt.rate, in_fmt.rate))
  assert (out_fmt.channels == in_fmt.channels, string.format (
      "channel counts differ: %d and %d", out_fmt.channels, in_fmt.channels))
  assert (out_fmt.position ~= nil and in_fmt.position ~= nil,
      "a stream was configured with no channel positions")
  for i, channel in ipairs (out_fmt.position) do
    assert (in_fmt.position [i] == channel, string.format (
        "channel %d differs: %s and %s", i, channel,
        tostring (in_fmt.position [i])))
  end
end

-- Every link has to join two ports of the same channel. Unpositioned ports
-- score best against the sink's control port, and pipewire refuses to
-- negotiate such a link.
local function checkLinks (stream_link, out_stream, in_stream)
  local out_channels = portChannels (out_stream)
  local in_channels = portChannels (in_stream)
  local linked = {}
  local count = 0

  for link in stream_link:iterate_links () do
    local props = link.properties
    local out_channel = out_channels [tonumber (props ["link.output.port"])]
    local in_channel = in_channels [tonumber (props ["link.input.port"])]

    assert (out_channel ~= nil, "the output port is not a port of the stream")
    assert (in_channel ~= nil, "the input port is not a port of the stream")
    assert (out_channel == in_channel, string.format (
        "linked %s to %s", out_channel, in_channel))

    linked [out_channel] = true
    count = count + 1
  end

  assert (count == 2, "expected 2 links, found " .. tostring (count))
  assert (linked ["FL"] and linked ["FR"], "not both channels were linked")
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

    if reconfigured or target == nil then
      return
    end

    assert (target.properties ["node.name"] == "default-device-node")
    local stream_link = tu.getStreamLink (stream, target)
    assert (stream_link ~= nil)

    checkSameFormat (stream, target)
    checkLinks (stream_link, stream, target)
    assert (not hasPort (target, "control"))
    reconfigured = true

    -- stream/configure-ports.lua reconfigures the whole link for this setting,
    -- which removes the ports the links were made of
    tu.setSetting ("stream.features.audio.control-port", "true", function ()
      tu.waitUntil (function ()
        return hasPort (target, "control") and
            stream_link:get_links_count () == 2
      end, function ()
        Log.info (stream_link, "relinked after the port reconfiguration")

        assert (stream:has_control_ports_enabled ())
        assert (target:has_control_ports_enabled ())
        checkSameFormat (stream, target)
        checkLinks (stream_link, stream, target)

        Script:finish_activation ()
      end)
    end)
  end
}:register ()
