-- WirePlumber
--
-- SPDX-License-Identifier: MIT
--
-- Tests for the route-availability scoring used to rank device profiles.

putils = require ("profile-utils")

-- Models the UCM card from wireplumber#683: two mutually exclusive HiFi
-- profiles, each bundling all three HDMI outputs with one of Speaker /
-- Headphones. The Speaker route never gets jack detection, so it stays
-- "unknown"; the Headphones and HDMI routes follow the jacks.
local HEADPHONES_PROFILE = {
  index = 1,
  name = "HiFi (HDMI1, HDMI2, HDMI3, Headphones, Mic1, Mic2)",
  priority = 10300,
}
local SPEAKER_PROFILE = {
  index = 2,
  name = "HiFi (HDMI1, HDMI2, HDMI3, Mic1, Mic2, Speaker)",
  priority = 10200,
}

function makeRoutes (hdmi1, headphones)
  return {
    { index = 10, direction = "Output", profiles = { 1, 2 }, available = hdmi1 },
    { index = 11, direction = "Output", profiles = { 1, 2 }, available = "no" },
    { index = 12, direction = "Output", profiles = { 1, 2 }, available = "no" },
    { index = 13, direction = "Output", profiles = { 1 }, available = headphones },
    { index = 14, direction = "Output", profiles = { 2 }, available = "unknown" },
    { index = 15, direction = "Input", profiles = { 1, 2 }, available = "unknown" },
  }
end

-- nothing plugged: only the Speaker profile offers a usable output
do
  local scores = putils.buildOutputRouteScores (makeRoutes ("no", "no"))
  assert (scores [1].yes == 0)
  assert (scores [1].unknown == 0)
  assert (scores [2].yes == 0)
  assert (scores [2].unknown == 1)
  assert (putils.compareProfiles (SPEAKER_PROFILE, HEADPHONES_PROFILE, scores))
  assert (not putils.compareProfiles (HEADPHONES_PROFILE, SPEAKER_PROFILE, scores))
end

-- monitor plugged, headphones unplugged: this is the #683 symptom; the
-- Headphones profile must not win just because it also carries HDMI1
do
  local scores = putils.buildOutputRouteScores (makeRoutes ("yes", "no"))
  assert (scores [1].yes == 1)
  assert (scores [1].unknown == 0)
  assert (scores [2].yes == 1)
  assert (scores [2].unknown == 1)
  assert (putils.compareProfiles (SPEAKER_PROFILE, HEADPHONES_PROFILE, scores))
end

-- headphones plugged, no monitor: both profiles offer one usable output,
-- so the priority decides and the Headphones profile is picked
do
  local scores = putils.buildOutputRouteScores (makeRoutes ("no", "yes"))
  assert (scores [1].yes == 1)
  assert (scores [2].yes == 0)
  assert (scores [2].unknown == 1)
  assert (putils.compareProfiles (HEADPHONES_PROFILE, SPEAKER_PROFILE, scores))
end

-- both plugged
do
  local scores = putils.buildOutputRouteScores (makeRoutes ("yes", "yes"))
  assert (scores [1].yes == 2)
  assert (scores [2].yes == 1)
  assert (putils.compareProfiles (HEADPHONES_PROFILE, SPEAKER_PROFILE, scores))
end

-- input routes are never counted
do
  local scores = putils.buildOutputRouteScores ({
    { index = 15, direction = "Input", profiles = { 1 }, available = "yes" },
  })
  assert (scores [1] == nil or (scores [1].yes == 0 and scores [1].unknown == 0))
end

-- routes that do not name their profiles belong to every profile, so they
-- cannot discriminate; ranking falls back to the profile priority
do
  local scores = putils.buildOutputRouteScores ({
    { index = 10, direction = "Output", available = "yes" },
    { index = 13, direction = "Output", available = "no" },
  })
  assert (putils.compareProfiles (HEADPHONES_PROFILE, SPEAKER_PROFILE, scores))
  assert (not putils.compareProfiles (SPEAKER_PROFILE, HEADPHONES_PROFILE, scores))
end

-- a device with no routes at all ranks on priority alone
do
  local scores = putils.buildOutputRouteScores ({})
  assert (putils.compareProfiles (HEADPHONES_PROFILE, SPEAKER_PROFILE, scores))
  assert (not putils.compareProfiles (SPEAKER_PROFILE, HEADPHONES_PROFILE, scores))
end

-- a profile is never "better" than itself
do
  local scores = putils.buildOutputRouteScores (makeRoutes ("yes", "yes"))
  assert (not putils.compareProfiles (HEADPHONES_PROFILE, HEADPHONES_PROFILE, scores))
end

-- Models a classic (non-UCM) ACP laptop card, where the analog and the HDMI
-- outputs live in separate profiles. The Speaker route has no jack of its own,
-- so it is never reported as available, while a connected monitor makes the
-- HDMI route available; the analog profile must still win on priority.
do
  local ANALOG_PROFILE = {
    index = 1,
    name = "output:analog-stereo+input:analog-stereo",
    priority = 6500,
  }
  local HDMI_PROFILE = {
    index = 2,
    name = "output:hdmi-stereo",
    priority = 5900,
  }
  local scores = putils.buildOutputRouteScores ({
    { index = 10, direction = "Output", profiles = { 1 }, available = "unknown" },
    { index = 11, direction = "Output", profiles = { 1 }, available = "no" },
    { index = 12, direction = "Output", profiles = { 2 }, available = "yes" },
    { index = 13, direction = "Input", profiles = { 1 }, available = "unknown" },
  })
  assert (scores [1].yes == 0)
  assert (scores [1].unknown == 1)
  assert (scores [2].yes == 1)
  assert (scores [2].unknown == 0)
  assert (putils.compareProfiles (ANALOG_PROFILE, HDMI_PROFILE, scores))
  assert (not putils.compareProfiles (HDMI_PROFILE, ANALOG_PROFILE, scores))
end

-- ... but once the headphones are plugged in, the Speaker route goes
-- unavailable and the analog profile has nowhere left to play
do
  local ANALOG_OUT_ONLY = { index = 1, name = "output:analog-stereo", priority = 6500 }
  local HDMI_PROFILE = { index = 2, name = "output:hdmi-stereo", priority = 5900 }
  local scores = putils.buildOutputRouteScores ({
    { index = 10, direction = "Output", profiles = { 1 }, available = "no" },
    { index = 12, direction = "Output", profiles = { 2 }, available = "yes" },
  })
  assert (putils.compareProfiles (HDMI_PROFILE, ANALOG_OUT_ONLY, scores))
end

-- profiles that tie on usable routes and on priority are separated by the
-- routes that are known to be available
do
  local A = { index = 1, name = "a", priority = 100 }
  local B = { index = 2, name = "b", priority = 100 }
  local scores = putils.buildOutputRouteScores ({
    { index = 10, direction = "Output", profiles = { 1 }, available = "yes" },
    { index = 11, direction = "Output", profiles = { 2 }, available = "unknown" },
  })
  assert (putils.compareProfiles (A, B, scores))
  assert (not putils.compareProfiles (B, A, scores))
end
