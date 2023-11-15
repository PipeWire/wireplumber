-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- Device settings manager

local settings_manager = require ("settings-manager")

local defaults = {
  ["use-persistent-storage"] = true,
  ["default-volume"] = 0.4 ^ 3,
  ["default-input-volume"] = 1.0,
  ["camera-discovery-timeout"] = 100,
}

return settings_manager.new ("device.", defaults)
