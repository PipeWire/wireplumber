-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- linking settings manager

local settings_manager = require ("settings-manager")

local defaults = {
  ["move"] = true,
  ["follow"] = true,
  ["filter-forward-format"] = false,
  ["duck-level"] = 0.3,
}

return settings_manager.new ("linking.default.", defaults)

