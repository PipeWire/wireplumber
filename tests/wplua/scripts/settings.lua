
--  tests the lua API of WpSettings, this file tests the settings present in
--  .conf file that is loaded.

-- test settings _get_boolean ()
local value = Settings.get_boolean ("test-property1", "test-settings")
assert (value == false)

value = Settings.get_boolean ("test-property2", "test-settings")
assert ("boolean" == type (value))
assert (value == true)

value = Settings.get_boolean ("test-property1")
assert (value == nil)

value = Settings.get_boolean ("test-property-undefined",
    "test-settings")
assert (value == nil)


-- test settings _get_int ()
value = Settings.get_int ("test-property-undefined", "test-settings")
assert (value == nil)

value = Settings.get_int ("test-property3-int", "test-settings")
assert ("number" == type (value))
assert (value == -20)

value = Settings.get_int ("test-property4-max-int", "test-settings")
assert (value == 9223372036854775807)

value = Settings.get_int ("test-property4-min-int", "test-settings")
assert (value == -9223372036854775808)

value = Settings.get_int ("test-property4-max-int-one-more",
    "test-settings")
assert (value == 0)

value = Settings.get_int ("test-property4-min-int-one-less",
    "test-settings")
assert (value == 0)

-- test settings _get_string ()
value = Settings.get_string ("test-property-undefined", "test-settings")
assert (value == nil)

value = Settings.get_string ("test-property4-string", "test-settings")
assert ("string" == type (value))
assert (value == "blahblah")

value = Settings.get_string ("test-property3-int", "test-settings")
assert (value == "-20")

-- test settings _get_float ()
value = Settings.get_float ("test-property-undefined", "test-settings")
assert (value == nil)

value = Settings.get_float ("test-property-float1", "test-settings")
assert ("number" == type (value))
assert ((value - 3.14) < 0.00001)

value = Settings.get_float ("test-property-float2", "test-settings")
assert ((value - 0.4) < 0.00001)


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

-- test #9
local cp = {
  ["test-string6-wildcard"] = "wild_cat",
}

local applied, ap = Settings.apply_rule ("rule_three", cp, "test-settings")

assert (applied == true)
assert (ap["prop.electrical.conductivity"] == "true")
assert (ap["prop.state"] == "solid")
assert (ap["prop.example"] == "ferrous")
