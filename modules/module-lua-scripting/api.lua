-- WirePlumber
--
-- This file contains the API that is made available to the Lua scripts
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

local function Constraint (spec)
  assert (type(spec[1]) == "string", "Constraint: expected subject as string");
  assert (type(spec[2]) == "string", "Constraint: expected verb as string");

  local subject = spec[1]
  local verb = spec[2]
  local verbs = {
    ["="] = "equals",
    ["!"] = "not-equals",
    ["c"] = "in-list",
    ["~"] = "in-range",
    ["#"] = "matches",
    ["+"] = "is-present",
    ["-"] = "is-absent"
  }

  -- check and convert verb to its short version
  local verb_is_valid = false
  for k, v in pairs(verbs) do
    if verb == k or verb == v then
      verb = k
      spec[2] = k
      verb_is_valid = true
      break
    end
  end
  assert (verb_is_valid, "Constraint: invalid verb '" .. verb .. "'")

  -- check and convert type to its integer value
  local type = spec["type"]
  if type then
    local valid_types = { "pw-global", "pw", "gobject" }
    local type_is_valid = false

    for i, v in ipairs(valid_types) do
      if type == v then
        spec["type"] = i
        type_is_valid = true
        break
      end
    end

    assert(type_is_valid, "Constraint: invalid subject type '" .. type .. "'")
  end

  -- check if we got the right amount of values
  if verb == "=" or verb == "!" or verb == "#" then
    assert (spec[3] ~= nil,
      "Constraint: " .. verbs[verb] .. ": expected constraint value")
  elseif verb == "c" then
    assert (spec[3] ~= nil,
      "Constraint: " .. verbs[verb] .. ": expected at least one constraint value")
  elseif verb == "~" then
    assert (spec[3] ~= nil and spec[4] ~= nil,
      "Constraint: " .. verbs[verb] .. ": expected two values")
  else
    assert (spec[3] == nil,
      "Constraint: " .. verbs[verb] .. ": expected no value, but there is one")
  end

  return debug.setmetatable(spec, { __name = "Constraint" })
end

local function dump_table(t, indent)
  local indent_str = ""
  indent = indent or 1
  for i = 1, indent, 1 do
    indent_str = indent_str .. "\t"
  end

  local kvpairs = {}
  for k, v in pairs(t) do
    table.insert(kvpairs, { k, v })
  end

  table.sort(kvpairs, function (lhs, rhs)
    local left_key, right_key = lhs[1], rhs[1]

    -- If the types are different, we sort by the type
    -- in alphabetical order. This means that numbers
    -- come before before strings, etc
    if type(left_key) ~= type(right_key) then
      return type(left_key) < type(right_key)
    end

    local key_type = type(left_key)

    -- Only numbers and strings have a well-defined order
    -- that's guaranteed to fulfill the requirements of
    -- table.sort (strict weak order)
    if key_type == "number" or key_type == "string" then
      return left_key < right_key
    end

    -- At this point, we have no good way to order the objects.
    -- We can't just do `left_key < right_key`, because this may fail
    -- if there's no `__lt` metamethod, and even if there is one,
    -- it might not be a strict weak order. (The Lua reference does
    -- not say what happens if the order is not strict weak, so it's
    -- undefined behaviour)

    -- That said, it's always mathematically "permitted" to return `false`,
    -- in which case, since both x < y and y < x are false, the elements
    -- are considered "equivalent" and may appear in any order in relation
    -- to each other. The elements are still sorted in relation to the
    -- *other* keys.
    return false

    -- To be a strict weak order, if x and y are equivalent, and y and z
    -- are equivalent, then x and z must be equivalent too. Otherwise the
    -- ordering is only a strict *partial* order.

    -- Note that the Lua 5.3 reference states that the order merely has to
    -- be a strict *partial* order, but since all weak orders are partial
    -- orders, this is not a problem.
  end)

  for _, pair in ipairs(kvpairs) do
    local k, v = table.unpack(pair)

    if (type(v) == "table") then
      print (indent_str .. tostring(k) .. ": ")
      dump_table(v, indent + 1)
    else
      print (indent_str .. tostring(k) .. ": " .. tostring(v))
    end
  end
end

local Debug = {
  dump_table = dump_table,
}

local Id = {
  INVALID = 0xffffffff,
  ANY = 0xffffffff,
}

local Features = {
  PipewireObject = {
    MINIMAL = 0x11,
  },
  ALL = 0xffffffff,
}

local Feature = {
  Proxy = {
    BOUND             = 1,
  },
  PipewireObject = {
    INFO              = (1 << 4),
    PARAM_PROPS       = (1 << 5),
    PARAM_FORMAT      = (1 << 6),
    PARAM_PROFILE     = (1 << 7),
    PARAM_PORT_CONFIG = (1 << 8),
    PARAM_ROUTE       = (1 << 9),
  },
  SpaDevice = {
    ENABLED           = (1 << 16),
  },
  Node = {
    PORTS             = (1 << 16),
  },
  Session = {
    ENDPOINTS         = (1 << 16),
    LINKS             = (1 << 17),
  },
  Endpoint = {
    STREAMS           = (1 << 16),
  },
  Metadata = {
    DATA              = (1 << 16),
  },
  SessionItem = {
    ACTIVE            = (1 << 0),
    EXPORTED          = (1 << 1),
  },
}

SANDBOX_EXPORT = {
  Debug = Debug,
  Id = Id,
  Features = Features,
  Feature = Feature,
  GLib = GLib,
  Log = WpLog,
  Core = WpCore,
  Plugin = WpPlugin,
  ObjectManager = WpObjectManager_new,
  Interest = WpObjectInterest_new,
  SessionItem = WpSessionItem_new,
  Constraint = Constraint,
  Device = WpDevice_new,
  SpaDevice = WpSpaDevice_new,
  Node = WpNode_new,
  LocalNode = WpImplNode_new,
  Link = WpLink_new,
  Pod = WpSpaPod,
  State = WpState_new,
  ImplModule = WpImplModule_new,
}
