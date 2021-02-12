-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

local config = {}
config.rules = {
  {
    matches = {
      {
        { "device.name", "matches", "bluez_card.*" },
      },
    },
    apply_properties = {
      ["device.nick"] = "My Device",
    },
  },
  {
    matches = {
      {
        { "node.name", "matches", "bluez_input.*" },
      },
      {
        { "node.name", "matches", "bluez_output.*" },
      },
    },
    apply_properties = {
      ["node.pause-on-idle"] = true,
    },
  },
}

for _, r in ipairs(config.rules or {}) do
  r.interests = {}
  for _, i in ipairs(r.matches) do
    local interest_desc = { type = "properties" }
    for _, c in ipairs(i) do
      c.type = "pw"
      table.insert(interest_desc, Constraint(c))
    end
    local interest = Interest(interest_desc)
    table.insert(r.interests, interest)
  end
  r.matches = nil
end

function rulesApplyProperties(properties)
  for _, r in ipairs(config.rules or {}) do
    if r.apply_properties then
      for _, interest in ipairs(r.interests) do
        if interest:matches(properties) then
          for k, v in pairs(r.apply_properties) do
            properties[k] = v
          end
        end
      end
    end
  end
end

local test1 = {
  ["node.name"] = "bluez_output.test1"
}
rulesApplyProperties(test1)
assert(test1["node.pause-on-idle"] == true)

local test2 = {
  ["device.name"] = "bluez_card.test2"
}
rulesApplyProperties(test2)
assert(test2["device.nick"] == "My Device")

local test3 = {
  ["device.name"] = "not_a_match"
}
rulesApplyProperties(test3)
assert(test3["device.nick"] == nil)
assert(test3["node.pause-on-idle"] == nil)
