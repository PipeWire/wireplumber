-- Endpoint support config file --

endpoint_support = {}

endpoint_support.sessions = {
  -- [session name] = { session properties }
  ["audio"] = {},
  ["video"] = {},
}

endpoint_support.policy = {
  move = true,   -- moves endpoints when metadata target.node changes
  follow = true  -- moves endpoints to the default device when it has changed
}

function endpoint_support.enable()
  -- Session item factories, building blocks for the session management graph
  -- Do not disable these unless you really know what you are doing
  load_module("si-node")
  load_module("si-audio-adapter")
  load_module("si-audio-convert")
  load_module("si-standard-link")

  -- Create sessions statically at startup
  load_script("static-sessions.lua", endpoint_support.sessions)

  -- Create items for nodes that appear in the graph
  load_script("create-item.lua")

  -- Link endpoints to each other to make media flow in the graph
  load_script("policy-endpoint.lua", endpoint_support.policy)
end
