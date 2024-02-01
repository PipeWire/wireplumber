-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- linking settings manager

local settings_manager = require ("settings-manager")

local defaults = {
  ["allow-moving-streams"] = true,
  ["follow-default-target"] = true,
}

return settings_manager.new ("linking.", defaults)
