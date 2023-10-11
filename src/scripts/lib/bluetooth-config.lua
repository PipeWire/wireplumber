-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

-- Bluetooth settings manager

local settings_manager = require ("settings-manager")

local defaults = {
  ["use-persistent-storage"] = true,
  ["autoswitch-to-headset-profile"] = true,
  ["autoswitch-applications"] = {
      "Firefox", "Chromium input", "Google Chrome input", "Brave input",
      "Microsoft Edge input", "Vivaldi input", "ZOOM VoiceEngine",
      "Telegram Desktop", "telegram-desktop", "linphone", "Mumble",
      "WEBRTC VoiceEngine", "Skype"
  }
}

return settings_manager.new ("bluetooth.", defaults)
