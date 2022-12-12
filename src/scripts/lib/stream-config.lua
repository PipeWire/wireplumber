-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- Device settings manager

local settings_manager = require ("settings-manager")

local defaults = {
  ["restore-props"] = true,
  ["restore-target"] = true,
  ["default-channel-volume"] = 1.0,
}

return settings_manager.new ("stream.", defaults)
