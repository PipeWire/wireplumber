-- Tests that the link between two streams is restored after another client
-- changes the port configuration of one of them. The ports that the links were
-- made of are replaced, and no configuration was requested through the stream,
-- so nothing reports the new ports as configured: the stream link has to bring
-- the other stream to the new configuration and link the new ports by itself.

local tu = require ("test-utils")

Script.async_activation = true

started = false

tu.createDeviceNode ("default-device-node", "Audio/Sink")

-- The audio channel of every port of the stream, keyed by the port's bound id
local function portChannels (stream)
  local channels = {}

  for port in stream:iterate_ports () do
    channels [port ["bound-id"]] =
        port.properties ["audio.channel"] or port.properties ["port.name"]
  end

  return channels
end

-- Whether the ports of the stream carry exactly these channels
local function hasChannels (stream, position)
  local found = {}
  local count = 0

  for _, channel in pairs (portChannels (stream)) do
    found [channel] = true
    count = count + 1
  end

  for _, channel in ipairs (position) do
    if not found [channel] then
      return false
    end
  end

  return count == #position
end

-- Every link has to join two ports that the streams have now, of the same
-- channel, and every channel has to be linked
local function checkLinks (stream_link, out_stream, in_stream, position)
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

  assert (count == #position, string.format ("expected %d links, found %d",
      #position, count))
  for _, channel in ipairs (position) do
    assert (linked [channel], channel .. " was not linked")
  end
end

-- Sets PortConfig on the node of the stream behind the stream's back, the way
-- another client would
local function setPortConfigExternally (stream, position)
  local node = stream:get_node ()
  local format = stream:get_ports_format ():parse ().properties

  -- PortConfig can only be set while the node is suspended
  node:send_command ("Suspend")
  node:set_param ("PortConfig", Pod.Object {
    "Spa:Pod:Object:Param:PortConfig", "PortConfig",
    direction = "Input",
    mode = "dsp",
    monitor = true,
    control = false,
    format = Pod.Object {
      "Spa:Pod:Object:Param:Format", "Format",
      mediaType = "audio",
      mediaSubtype = "raw",
      format = format.format,
      rate = format.rate,
      channels = #position,
      position = Pod.Array { "Spa:Enum:AudioChannel", table.unpack (position) },
    },
  })
end

-- Changes the port configuration of the target and waits until the same
-- stream link has linked the new ports
local function changeAndCheck (stream_link, stream, target, position, cb)
  setPortConfigExternally (target, position)

  tu.waitUntil (function ()
    return hasChannels (target, position) and hasChannels (stream, position) and
        stream_link:get_links_count () == #position
  end, function ()
    Log.info (stream_link, string.format (
        "relinked %d channel(s) after the external port configuration",
        #position))

    assert (tu.getStreamLink (stream, target) == stream_link)
    checkLinks (stream_link, stream, target, position)
    cb ()
  end)
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

    if started or target == nil then
      return
    end

    assert (target.properties ["node.name"] == "default-device-node")
    local stream_link = tu.getStreamLink (stream, target)
    assert (stream_link ~= nil)

    checkLinks (stream_link, stream, target, { "FL", "FR" })
    started = true

    -- a different channel layout, and then back to the first one
    changeAndCheck (stream_link, stream, target, { "MONO" }, function ()
      changeAndCheck (stream_link, stream, target, { "FL", "FR" }, function ()
        Script:finish_activation ()
      end)
    end)
  end
}:register ()
