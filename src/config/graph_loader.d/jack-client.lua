--
-- JACK client pseudo-device
--
-- This device allows PipeWire to connect to a real JACK server as a client
--

static_object {
  type = "device",
  factory = "spa-device-factory",
  properties = {
    ["factory.name"] = "api.jack.device",
  }
}
