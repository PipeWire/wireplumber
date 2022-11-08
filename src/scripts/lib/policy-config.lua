-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- Policy configuration manager

local defaults <const> = {
  move = true,
  follow = true,
}

local keys <const> = {
  move = "policy.default.move",
  follow = "policy.default.follow",
}

local config = {
  move = Settings.parse_boolean_safe (keys.move, defaults.move),
  follow = Settings.parse_boolean_safe (keys.follow, defaults.follow),
}

Settings.subscribe ("policy.default*", function (_, setting, value)
  local parsed_val = value:parse ()
  if setting == keys.move then
    if type (parsed_val) == "boolean" and config.move ~= parsed_val then
      config.move = parsed_val
    end
  elseif setting == keys.follow then
    if type (parsed_val) == "boolean" and config.follow ~= parsed_val then
      config.follow = parsed_val
    end
  end
end)

return config
