
--  tests the lua API of WpSettings, this file tests the settings present in
--  .conf file that is loaded.

Script.async_activation = true

-- test settings _get_boolean ()

local value = Settings.get_boolean ("test-setting1", "test-settings")
assert (value == false)

value = Settings.get_boolean ("test-setting2", "test-settings")
assert ("boolean" == type (value))
assert (value == true)

value = Settings.get_boolean ("test-setting1")
assert (value == nil)

value = Settings.get_boolean ("test-setting-undefined",
    "test-settings")
assert (value == nil)


-- test settings _get_int ()
value = Settings.get_int ("test-setting-undefined", "test-settings")
assert (value == nil)

value = Settings.get_int ("test-setting3-int", "test-settings")
assert ("number" == type (value))
assert (value == -20)

-- test settings _get_string ()
value = Settings.get_string ("test-setting-undefined", "test-settings")
assert (value == nil)

value = Settings.get_string ("test-setting4-string", "test-settings")
assert ("string" == type (value))
assert (value == "blahblah")

value = Settings.get_string ("test-setting3-int", "test-settings")
assert (value == "-20")

-- test settings _get_float ()
value = Settings.get_float ("test-setting-undefined", "test-settings")
assert (value == nil)

value = Settings.get_float ("test-setting-float1", "test-settings")
assert ("number" == type (value))
assert ((value - 3.14) < 0.00001)

value = Settings.get_float ("test-setting-float2", "test-settings")
assert ((value - 0.4) < 0.00001)

-- test settings _get ()
value = Settings.get ("test-setting-json", "test-settings")
assert (value ~= nil)
assert (value:is_array())
assert (value:get_data() == "[1, 2, 3]")

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
local setting_type
local finish_activation

function callback (obj, s, rawvalue)
  if (setting_type == "boolean") then
    assert (s == setting)
    callback = true
    assert (rawvalue == tostring(setting_value))
    assert ((setting_value and true or false) ==
        Settings.get_boolean (s, "test-settings"))

  elseif (setting_type == "integer") then
    assert (s == setting)
    callback = true
    assert (rawvalue == tostring(setting_value))
    assert (setting_value ==
        Settings.get_int (s, "test-settings"))

  elseif (setting_type == "string") then
    assert (s == setting)
    callback = true
    assert (rawvalue == setting_value)
    assert (setting_value ==
        Settings.get_string (s, "test-settings"))
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
  setting_value = true
  callback = false
  setting_type = "boolean"

  metadata:set(0, setting, "Spa:String:JSON", tostring(setting_value))
  assert (callback)

  -- test #2
  setting = "test-setting1"
  setting_value = true
  callback = false
  setting_type = "boolean"

  metadata:set(0, setting, "Spa:String:JSON", tostring(setting_value))
  assert (not callback)

  -- test #3
  setting = "test-setting3-int"
  setting_value = 99
  callback = false
  setting_type = "integer"

  metadata:set(0, setting, "Spa:String:JSON", setting_value)
  assert (callback)

  -- test #4
  setting = "test-setting4-string"
  setting_value = "lets not blabber"
  callback = false
  setting_type = "string"

  finish_activation = true
  metadata:set(0, setting, "Spa:String:JSON", setting_value)
  assert (callback)

end)
