-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- Node settings manager

local settings_manager = require ("settings-manager")

local defaults = {
  ["features.audio.no-dsp"] = false,
  ["features.audio.monitor-ports"] = true,
  ["features.audio.control-port"] = false,

  ["stream.restore-props"] = true,
  ["stream.restore-target"] = true,
  ["stream.default-playback-volume"] = 1.0,
  ["stream.default-capture-volume"] = 1.0,

  ["filter.forward-format"] = false,

  ["restore-default-targets"] = true,
}

return settings_manager.new ("node.", defaults)
