-- Endpoint support config file --

endpoint_support = {}

endpoint_support.sessions = {
  -- [session name] = { session properties }
  ["audio"] = {},
  ["video"] = {},
}

function endpoint_support.enable()
  -- Session item factories, building blocks for the session management graph
  -- Do not disable these unless you really know what you are doing
  load_module("si-adapter")
  load_module("si-audio-softdsp-endpoint")
  load_module("si-bluez5-endpoint")
  load_module("si-convert")
  load_module("si-fake-stream")
  load_module("si-monitor-endpoint")
  load_module("si-simple-node-endpoint")
  load_module("si-standard-link")

  -- Create sessions statically at startup
  load_script("static-sessions.lua", endpoint_support.sessions)

  -- Create endpoints for nodes that appear in the graph
  load_script("create-endpoint.lua")

  -- Link endpoints to each other to make media flow in the graph
  load_script("policy-endpoint.lua")
end
