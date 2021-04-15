-- Default policy config file --

default_policy = {}

default_policy.sessions = {
  -- [session name] = { session properties }
  ["audio"] = { ["media.type"] = "Audio" },
  ["video"] = { ["media.type"] = "Video" },
}

default_policy.endpoints = {
  -- [endpoint name] = { endpoint properties }
--[[
  ["endpoint.multimedia"] = {
    ["media.class"] = "Audio/Sink",
    ["role"] = "Multimedia",
  },
  ["endpoint.notifications"] = {
    ["media.class"] = "Audio/Sink",
    ["role"] = "Notification",
  },
  ["endpoint.alert"] = {
    ["media.class"] = "Audio/Sink",
    ["role"] = "Alert",
  },
--]]
}

default_policy.policy = {
  ["move"] = true,   -- moves session items when metadata target.node changes
  ["follow"] = true, -- moves session items to the default device when it has changed

--[[
  ["roles"] = {
    ["Multimedia"] = {
      ["alias"] = { "Movie", "Music", "Game" },
      ["priority"] = 10,
      ["action.default"] = "mix",
    },
    ["Notification"] = {
      ["priority"] = 20,
      ["action.default"] = "cork",
      ["action.Notification"] = "mix",
    },
    ["Alert"] = {
      ["priority"] = 30,
      ["action.default"] = "cork",
      ["action.Alert"] = "mix",
    },
  },
--]]
}

function default_policy.enable()
  -- Session item factories, building blocks for the session management graph
  -- Do not disable these unless you really know what you are doing
  load_module("si-node")
  load_module("si-audio-adapter")
  load_module("si-standard-link")
  load_module("si-audio-endpoint")

  -- Create sessions statically at startup
  load_script("static-sessions.lua", default_policy.sessions)

  -- Create endpoints statically at startup
  load_script("static-endpoints.lua", default_policy.endpoints)

  -- Create items for nodes that appear in the graph
  load_script("create-item.lua")

  -- Link nodes to each other to make media flow in the graph
  load_script("policy-node.lua", default_policy.policy)

  -- Link client nodes with endpoints to make media flow in the graph
  load_script("policy-endpoint-client.lua", default_policy.policy)
  load_script("policy-endpoint-client-links.lua", default_policy.policy)

  -- Link endpoints with device nodes to make media flow in the graph
  load_script("policy-endpoint-device.lua", default_policy.policy)
end
