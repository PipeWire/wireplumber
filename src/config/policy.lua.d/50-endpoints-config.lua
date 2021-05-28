-- uncomment to enable role-based endpoints
-- this is not yet ready for desktop use
--
--[[

default_policy.policy.roles = {
  ["Multimedia"] = {
    ["alias"] = { "Movie", "Music", "Game" },
    ["priority"] = 10,
    ["action.default"] = "mix",
  },
  ["Notification"] = {
    ["priority"] = 20,
    ["action.default"] = "duck",
    ["action.Notification"] = "mix",
  },
  ["Alert"] = {
    ["priority"] = 30,
    ["action.default"] = "cork",
    ["action.Alert"] = "mix",
  },
}

default_policy.endpoints = {
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
}

]]--
