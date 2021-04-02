-- Default policy config file --

default_policy = {}

default_policy.sessions = {
  -- [session name] = { session properties }
  ["audio"] = { ["media.type"] = "Audio" },
  ["video"] = { ["media.type"] = "Video" },
}

default_policy.policy = {
  move = true,   -- moves session items when metadata target.node changes
  follow = true  -- moves session items to the default device when it has changed
}

function default_policy.enable()
  -- Session item factories, building blocks for the session management graph
  -- Do not disable these unless you really know what you are doing
  load_module("si-node")
  load_module("si-audio-adapter")
  load_module("si-audio-convert")
  load_module("si-standard-link")

  -- Create sessions statically at startup
  load_script("static-sessions.lua", default_policy.sessions)

  -- Create items for nodes that appear in the graph
  load_script("create-item.lua")

  -- Link nodes to each other to make media flow in the graph
  load_script("policy-node.lua", default_policy.policy)
end
