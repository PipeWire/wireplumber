-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- Bluetooth settings manager

local settings_manager = require ("settings-manager")

local defaults = {
  ["use-persistent-storage"] = true,
  ["autoswitch-to-headset-profile"] = true
}

return settings_manager.new ("bluetooth.", defaults)
