
--  tests the lua API of WpSettings, this file tests the settings present in
--  .conf file that is loaded.

-- test settings
assert (Settings.get_boolean ("test-property1", "test-settings") == false)
assert (Settings.get_boolean ("test-property2", "test-settings") == true)

assert (Settings.get_boolean ("test-property1") == false)

-- test rules
-- test #1
local cp = {
  ["test-string2"]="juggler"
}
local ap = {}

local applied, ap = Settings.apply_rule( "rule_one", cp, "test-settings")

assert (applied == true)
assert (ap["prop-string1"] == "metal")
assert (ap["prop-int1"] == "123")
assert (ap["blah blah"] == nil)

-- test #2
local cp = {
  ["test-string2"]="jugler"
}
local ap = {}

local applied, ap = Settings.apply_rule ("rule_one", cp, "test-settings")

assert (applied == false)

-- test #3
local cp = {
  ["test-string4"] = "ferrous",
  ["test-int2"] = "100",
  ["test-string5"] = "blend"
}

local applied, ap = Settings.apply_rule ("rule_one", cp, "test-settings")

assert (applied == true)
assert (ap["prop-string1"] == nil)
assert (ap["prop-string2"] == "standard")
assert (ap["prop-int2"] == "26")
assert (ap["prop-bool1"] == "true")

-- test #4
local cp = {
  ["test-string6"] = "alum",
}

local applied, ap = Settings.apply_rule ("rule_one", cp, "test-settings")

assert (applied == false)

-- test #5
local cp = {
  ["test-string6"] = "alum",
  ["test-int3"] = "24",
}

local applied, ap = Settings.apply_rule ("rule_one", cp, "test-settings")

assert (applied == true)
assert (ap["prop-string1"] == nil)
assert (ap["prop-string2"] == "standard")
assert (ap["prop-int2"] == "26")
assert (ap["prop-bool1"] == "true")

-- test #6
-- test regular expression syntax
local cp = {
  ["test.string6"] = "metal.ferrous",
  ["test.table.entry"] = "yes",
}

local applied, ap = Settings.apply_rule ("rule_three", cp, "test-settings")

assert (applied == false)

-- test #7
local cp = {
  ["test.string6"] = "metal.ferrous",
  ["test.table.entry"] = "true",
}

local applied, ap = Settings.apply_rule ("rule_three", cp, "test-settings")

assert (applied == true)
assert (ap["prop.electrical.conductivity"] == "true")
assert (ap["prop.state"] == "solid")
assert (ap["prop.example"] == "ferrous")

-- test #8
local cp = {
  ["test.string6"] = "gas.xenon",
  ["test.table.entry"] = "maybe",
}

local applied, ap = Settings.apply_rule ("rule_three", cp, "test-settings")

assert (applied == true)
assert (ap["prop.electrical.conductivity"] == "false")
assert (ap["prop.state"] == "gas")
assert (ap["prop.example"] == "neon")

