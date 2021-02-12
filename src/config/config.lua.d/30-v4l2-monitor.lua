-- V4L2 monitor config file --

local properties = { }

local rules = {
  -- An array of matches/actions to evaluate.
  {
    -- Rules for matching a device or node. It is an array of
    -- properties that all need to match the regexp. If any of the
    -- matches work, the actions are executed for the object.
    matches = {
      {
        -- This matches all cards.
        { "device.name", "matches", "v4l2_device.*" },
      },
    },
    -- Apply properties on the matched object.
    apply_properties = {
      -- ["device.nick"] = "My Device",
    },
  },
  {
    matches = {
      {
        -- Matches all sources.
        { "node.name", "matches", "v4l2_input.*" },
      },
      {
        -- Matches all sinks.
        { "node.name", "matches", "v4l2_output.*" },
      },
    },
    apply_properties = {
      --["node.nick"] = "My Node",
      --["priority.driver"] = 100,
      --["priority.session"] = 100,
      --["node.pause-on-idle"] = false,
    },
  },
}

function enable_v4l2()
  load_monitor("v4l2", {
    properties = properties,
    rules = rules,
  })
end
