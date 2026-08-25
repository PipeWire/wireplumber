-- WirePlumber

-- Copyright © 2026 Collabora Ltd.

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of utility functions for ranking device profiles

local putils = {}

local NO_ROUTES = { yes = 0, unknown = 0 }

-- Counts, for every profile index, the output routes that profile offers,
-- split by whether they are known to be available ("yes") or merely not known
-- to be unavailable ("unknown"). Routes that do not name the profiles they
-- belong to apply to every profile, so they cannot tell profiles apart and are
-- skipped; the same goes for input routes, whose availability says nothing
-- about where playback can go.
function putils.buildOutputRouteScores (routes)
  local scores = {}

  for _, route in ipairs (routes) do
    if route.direction == "Output" and route.profiles then
      local available = route.available or "unknown"

      for _, index in ipairs (route.profiles) do
        index = tonumber (index)
        local score = scores [index]

        if not score then
          score = { yes = 0, unknown = 0 }
          scores [index] = score
        end

        if available == "yes" then
          score.yes = score.yes + 1
        elseif available ~= "no" then
          score.unknown = score.unknown + 1
        end
      end
    end
  end

  return scores
end

-- Returns true if profile 'a' should be preferred over profile 'b'.
--
-- A profile's "available" flag is set if any single one of its routes is
-- available, which on cards that bundle HDMI outputs together with the
-- speakers or the headphones is true almost all the time. Ranking on the
-- priority number alone therefore keeps selecting profiles whose analog output
-- is not plugged in.
--
-- Compare first on how many output routes the profile can actually play on,
-- which is the test that decides whether a profile is available at all,
-- applied per profile and counted instead of OR'ed. Routes of unknown
-- availability have to count here: built-in speakers get no jack detection and
-- are never reported as available, so counting only the available ones would
-- rank a profile offering a connected monitor above the one offering the
-- speakers.
--
-- The priority breaks the tie, which keeps the behaviour unchanged for devices
-- that expose no per-profile route information, and the number of routes known
-- to be available breaks it further when the priorities are equal as well.
function putils.compareProfiles (a, b, scores)
  local sa = scores [tonumber (a.index)] or NO_ROUTES
  local sb = scores [tonumber (b.index)] or NO_ROUTES
  local usable_a = sa.yes + sa.unknown
  local usable_b = sb.yes + sb.unknown
  local priority_a = a.priority or 0
  local priority_b = b.priority or 0

  if usable_a ~= usable_b then
    return usable_a > usable_b
  elseif priority_a ~= priority_b then
    return priority_a > priority_b
  else
    return sa.yes > sb.yes
  end
end

return putils
