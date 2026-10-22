-- Tests for the ranking that elects the default node.

nutils = require ("node-utils")

-- Mirrors the selection loop of default-streams/find-best-default-stream.
function elect (rankings)
  local selected = nil

  for _, ranking in ipairs (rankings) do
    if selected == nil or nutils.compare_nodes (ranking, selected) then
      selected = ranking
    end
  end

  return selected
end

-- Runs elect() over every permutation of the given rankings and asserts that
-- they all agree. The nodes reach the hook through a WpObjectManager, which
-- swaps its last element into the freed slot on every removal, so the order
-- is not the caller's to rely on.
function electInAnyOrder (rankings)
  local winners = {}

  local function permute (rest, order)
    if #rest == 0 then
      table.insert (winners, elect (order))
      return
    end
    for i = 1, #rest do
      local head = table.remove (rest, i)
      table.insert (order, head)
      permute (rest, order)
      table.remove (order)
      table.insert (rest, i, head)
    end
  end

  permute ({table.unpack (rankings)}, {})
  assert (#winners > 1)
  for _, winner in ipairs (winners) do
    assert (winner == winners [1])
  end

  return winners [1]
end

-- https://lf-automotivelinux.atlassian.net/browse/SPEC-5339
-- Models the Raspberry Pi 4 of AGL SPEC-5339: the analog jack and the HDMI
-- output live on two separate cards and tie at priority.session 1000, because
-- the analog card has no ACP mixer mapping and so its profile is named
-- "stereo-fallback" instead of "analog-stereo" and misses the +9 bonus that
-- alsa.lua gives analog profiles. ACP prices the analog-output route at 9900
-- and hdmi-output-0 at 5900 on every card that has them.
local JACK = {
  priority = 1000, group_serial = 69, route_priority = 9900, serial = 104,
}
local HDMI = {
  priority = 1000, group_serial = 70, route_priority = 5900, serial = 105,
}

-- the route priority belongs to two different cards here, so it must not be
-- consulted; the older card wins and the outcome does not depend on the order
do
  assert (electInAnyOrder ({ JACK, HDMI }) == JACK)
  assert (nutils.compare_nodes (JACK, HDMI))
  assert (not nutils.compare_nodes (HDMI, JACK))
end

-- ... and the same two cards enumerated the other way round elect the other
-- one, rather than always electing whichever card carries an analog output
do
  local jack = { priority = 1000, group_serial = 70, route_priority = 9900, serial = 177 }
  local hdmi = { priority = 1000, group_serial = 69, route_priority = 5900, serial = 153 }
  assert (electInAnyOrder ({ jack, hdmi }) == hdmi)
end

-- the session priority outranks everything below it
do
  local speakers = { priority = 1009, group_serial = 70, route_priority = 100, serial = 105 }
  assert (electInAnyOrder ({ JACK, speakers }) == speakers)
end

-- two nodes of one card do reach the route priority: this is the case
-- a433a49e was written for
do
  local speaker = { priority = 1000, group_serial = 42, route_priority = 10000, serial = 50 }
  local spdif = { priority = 1000, group_serial = 42, route_priority = 5000, serial = 51 }
  assert (electInAnyOrder ({ spdif, speaker }) == speaker)
end

-- two identical USB DACs tie on every priority key; the older one wins and
-- keeps winning whatever order the object manager hands them over in
do
  local first = { priority = 1109, group_serial = 80, route_priority = 9900, serial = 81 }
  local second = { priority = 1109, group_serial = 90, route_priority = 9900, serial = 91 }
  assert (electInAnyOrder ({ first, second }) == first)
end

-- nodes of the same card that tie on the route priority as well
do
  local a = { priority = 1000, group_serial = 42, route_priority = 9900, serial = 50 }
  local b = { priority = 1000, group_serial = 42, route_priority = 9900, serial = 51 }
  assert (electInAnyOrder ({ b, a }) == a)
end

-- a node is never better than itself
do
  assert (not nutils.compare_nodes (JACK, JACK))
end

-- the ranking is a total order, so three cards elect the same winner however
-- they are shuffled
do
  local third = { priority = 1000, group_serial = 71, route_priority = 9900, serial = 106 }
  assert (electInAnyOrder ({ JACK, HDMI, third }) == JACK)
  assert (electInAnyOrder ({ HDMI, third }) == HDMI)
end

-- a node selected by an earlier hook is given mininteger serials so that it
-- keeps the selection against anything that merely ties on the priority
do
  local configured = {
    priority = 31000, group_serial = math.mininteger,
    route_priority = 0, serial = math.mininteger,
  }
  local tied = {
    priority = 31000, group_serial = 1, route_priority = 15000, serial = 1,
  }
  assert (not nutils.compare_nodes (tied, configured))
  assert (nutils.compare_nodes (configured, tied))
end

-- nodes that report no object.serial sort last and cannot be told apart, so
-- the first one seen is kept
do
  local a = {
    priority = 1000, group_serial = math.maxinteger,
    route_priority = 0, serial = math.maxinteger,
  }
  local b = {
    priority = 1000, group_serial = math.maxinteger,
    route_priority = 0, serial = math.maxinteger,
  }
  assert (not nutils.compare_nodes (a, b))
  assert (not nutils.compare_nodes (b, a))
  assert (elect ({ a, b }) == a)
  assert (elect ({ b, a }) == b)
end
