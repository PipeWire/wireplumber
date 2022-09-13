
--  tests the lua API of WpSettings, this file tests the settings present in
--  .conf file that is loaded.

Script.async_activation = true

-- test settings undefined
value = Settings.get ("test-setting-undefined", "test-settings")
assert (value == nil)

value = Settings.get ("test-setting1")
assert (value == nil)

-- test settings _get_boolean ()

local value = Settings.get ("test-setting1", "test-settings"):parse()
assert (value == false)

value = Settings.get ("test-setting2", "test-settings"):parse()
assert ("boolean" == type (value))
assert (value == true)

value = Settings.parse_boolean_safe ("test-setting2", false, "test-settings")
assert (value == true)

value = Settings.parse_boolean_safe ("test-setting-undefined", true, "test-settings")
assert (value == true)

-- test settings _get_int ()

value = Settings.get ("test-setting3-int", "test-settings"):parse()
assert ("number" == type (value))
assert (value == -20)

value = Settings.parse_int_safe ("test-setting3-int", 10, "test-settings")
assert (value == -20)

value = Settings.parse_int_safe ("test-setting-undefined", 10, "test-settings")
assert (value == 10)

-- test settings _get_string ()

value = Settings.get ("test-setting4-string", "test-settings"):parse()
assert ("string" == type (value))
assert (value == "blahblah")

value = Settings.get ("test-setting5-string-with-quotes", "test-settings"):parse()
assert ("string" == type (value))
assert (value == "a string with \"quotes\"")

value = Settings.parse_string_safe ("test-setting4-string", "fallback-string", "test-settings")
assert (value == "blahblah")

value = Settings.parse_string_safe ("test-setting-undefined", "fallback-string", "test-settings")
assert (value == "fallback-string")

-- test settings _get_float ()

value = Settings.get ("test-setting-float1", "test-settings"):parse()
assert ("number" == type (value))
assert ((value - 3.14) < 0.00001)

value = Settings.get ("test-setting-float2", "test-settings"):parse()
assert ((value - 0.4) < 0.00001)

value = Settings.parse_float_safe ("test-setting-float1", 4.14, "test-settings")
assert ((value - 3.14) < 0.00001)

value = Settings.parse_float_safe ("test-setting-undefined", 4.14, "test-settings")
assert ((value - 4.14) < 0.00001)

-- test settings _get ()
value = Settings.get ("test-setting-json", "test-settings")
assert (value ~= nil)
assert (value:is_array())
assert (value:get_data() == "[1, 2, 3]")

value = Settings.parse_array_safe ("test-setting-json", "test-settings")
assert (value ~= nil)
assert (value[1] == 1)
assert (value[2] == 2)
assert (value[3] == 3)

value = Settings.parse_array_safe ("test-setting-undefined", "test-settings")
assert (value ~= nil)
assert (#value == 0)

value = Settings.get ("test-setting-json2", "test-settings")
assert (value ~= nil)
assert (value:is_array())
assert (value:get_data() ==
  "[\"test1\", \"test 2\", \"test three\", \"test-four\"]")
val = value:parse ()
assert (val[1] == "test1")
assert (val[2] == "test 2")
assert (val[3] == "test three")
assert (val[4] == "test-four")
assert (val[5] == nil)
assert (#val == 4)

value = Settings.get ("test-setting-json3", "test-settings")
assert (value ~= nil)
assert (value:is_object())
print (value:get_data())
assert (value:get_data() ==
  "{ key1: \"value\", key2: 2, key3: true }")
val = value:parse ()
assert (val.key1 == "value")
assert (val.key2 == 2)
assert (val.key3 == true)

value = Settings.parse_object_safe ("test-setting-json3", "test-settings")
assert (value ~= nil)
assert (value.key1 == "value")
assert (value.key2 == 2)
assert (value.key3 == true)

value = Settings.parse_object_safe ("test-setting-undefined", "test-settings")
assert (value ~= nil)
assert (#value == 0)

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

-- test callbacks

metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "test-settings" },
  }
}

metadata_om:activate()

local setting
local setting_value
local callback
local finish_activation

function callback (obj, s, json)
  assert (json ~= nil)

  if (json:is_boolean()) then
    assert (s == setting)
    callback = true
    assert (json:parse() == setting_value:parse())
    assert (setting_value:parse() == Settings.get (s, "test-settings"):parse())

  elseif (json:is_int()) then
    assert (s == setting)
    callback = true
    assert (json:parse() == setting_value:parse())
    assert (setting_value:parse() == Settings.get (s, "test-settings"):parse())

  elseif (json:is_string()) then
    assert (s == setting)
    callback = true
    assert (json:parse() == setting_value:parse())
    assert (setting_value:parse() == Settings.get (s, "test-settings"):parse())
  end

  if (finish_activation) then
    assert (Settings.unsubscribe (sub_id, "test-settings"))
    assert (not Settings.unsubscribe (sub_id-1, "test-settings"))
    Script:finish_activation()
  end
end

sub_id = Settings.subscribe ("test*", "test-settings", callback)

metadata_om:connect("objects-changed", function (om)
  local metadata = om:lookup()

  if (not metadata) then
    return
  end

  -- test #1
  setting = "test-setting1"
  setting_value = Json.Boolean (true)
  callback = false

  metadata:set(0, setting, "Spa:String:JSON", setting_value:get_data())
  assert (callback)

  -- test #2
  setting = "test-setting1"
  setting_value = Json.Boolean (true)
  callback = false

  metadata:set(0, setting, "Spa:String:JSON", setting_value:get_data())
  assert (not callback)

  -- test #3
  setting = "test-setting3-int"
  setting_value = Json.Int (99)
  callback = false

  metadata:set(0, setting, "Spa:String:JSON", setting_value:get_data())
  assert (callback)

  -- test #4
  setting = "test-setting4-string"
  setting_value = Json.String ("lets not blabber")
  callback = false

  finish_activation = true
  metadata:set(0, setting, "Spa:String:JSON", setting_value:get_data())
  assert (callback)

end)
