--
-- ALSA midi bridge
--

objects["midi"] = {
  type = "node",
  factory = "spa-node-factory",
  properties = {
    ["factory.name"] = "api.alsa.seq.bridge",
    ["node.name"] = "MIDI Bridge",
  }
}
