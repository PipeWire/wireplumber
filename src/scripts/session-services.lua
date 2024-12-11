-- WirePlumber
--
-- Copyright © 2024 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

local features_to_services = {
  ["monitor.alsa"] = { "audio", "api.alsa" },
  ["monitor.alsa-midi"] = { "midi", "api.alsa-seq" },
  ["monitor.bluez"] = { "bluetooth.audio", "api.bluez" },
  ["monitor.bluez-midi"] = { "bluetooth.midi", "api.bluez" },
  ["monitor.v4l2"] = { "video-capture", "api.v4l2" },
  ["monitor.libcamera"] = { "video-capture", "api.libcamera" },
  ["policy.device.profile"] = { "policy.device.profile" },
  ["policy.device.routes"] = { "policy.device.routes" },
  ["policy.default-nodes"] = { "policy.default-nodes" },
  ["policy.linking.standard"] = { "policy.linking.standard" },
  ["policy.linking.role-based"] = { "policy.linking.role-based" },
}

local services = {}
for k, v in pairs (features_to_services) do
  if Core.test_feature (k) then
    for _, s in pairs (v) do
      services[s] = s
    end
  end
end

-- convert to array with numeric indices
local services_array = {}
for _, s in pairs (services) do
  table.insert (services_array, s)
end

Core.update_properties {
  ["session.services"] = "[" .. table.concat (services_array, ", ") .. "]"
}
