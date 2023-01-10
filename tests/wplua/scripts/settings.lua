
--  tests the lua API of WpSettings, this file tests the settings present in
--  .conf file that is loaded.

Script.async_activation = true

-- test settings undefined
value = Settings.get ("test-setting-undefined")
assert (value == nil)

-- test settings _get_boolean ()

local value = Settings.get ("test-setting1"):parse()
assert (value == false)

value = Settings.get ("test-setting2"):parse()
assert ("boolean" == type (value))
assert (value == true)

-- test settings _get_int ()

value = Settings.get ("test-setting3-int"):parse()
assert ("number" == type (value))
assert (value == -20)

-- test settings _get_string ()

value = Settings.get ("test-setting4-string"):parse()
assert ("string" == type (value))
assert (value == "blahblah")

value = Settings.get ("test-setting5-string-with-quotes"):parse()
assert ("string" == type (value))
assert (value == "a string with \"quotes\"")

-- test settings _get_float ()

value = Settings.get ("test-setting-float1"):parse()
assert ("number" == type (value))
assert ((value - 3.14) < 0.00001)

value = Settings.get ("test-setting-float2"):parse()
assert ((value - 0.4) < 0.00001)

-- test settings _get ()
value = Settings.get ("test-setting-json")
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

value = Settings.get ("test-setting-json3")
assert (value ~= nil)
assert (value:is_object())
print (value:get_data())
assert (value:get_data() ==
  "{ key1: \"value\", key2: 2, key3: true }")
val = value:parse ()
assert (val.key1 == "value")
assert (val.key2 == 2)
assert (val.key3 == true)

-- test callbacks

metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "sm-settings" },
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
    assert (setting_value:parse() == Settings.get (s):parse())

  elseif (json:is_int()) then
    assert (s == setting)
    callback = true
    assert (json:parse() == setting_value:parse())
    assert (setting_value:parse() == Settings.get (s):parse())

  elseif (json:is_string()) then
    assert (s == setting)
    callback = true
    assert (json:parse() == setting_value:parse())
    assert (setting_value:parse() == Settings.get (s):parse())
  end

  if (finish_activation) then
    assert (Settings.unsubscribe (sub_id))
    assert (not Settings.unsubscribe (sub_id-1))
    Script:finish_activation()
  end
end

sub_id = Settings.subscribe ("test*", callback)

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
