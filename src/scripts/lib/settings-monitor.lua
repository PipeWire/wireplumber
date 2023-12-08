-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- Monitors settings manager

local settings_manager = require ("settings-manager")

local defaults = {
  ["camera-discovery-timeout"] = 100,
}

return settings_manager.new ("monitor.", defaults)
