
--  tests the lua API of WpSettings, this file tests the settings present in
--  .conf file that is loaded.

Script.async_activation = true

-- Undefined
value = Settings.get ("test-setting-undefined")
assert (value == nil)

-- Bool
value = Settings.get_boolean ("test-setting-bool")
assert ("boolean" == type (value))
assert (value == true)
value = Settings.get_boolean ("test-setting-bool-undefined")
assert ("boolean" == type (value))
assert (value == false)

-- Int
value = Settings.get_int ("test-setting-int")
assert ("number" == type (value))
assert (value == -20)
value = Settings.get_int ("test-setting-int-undefined")
assert ("number" == type (value))
assert (value == 0)

-- Float
value = Settings.get_float ("test-setting-float")
assert ("number" == type (value))
assert ((value - 3.14) < 0.00001)
value = Settings.get_float ("test-setting-float-undefined")
assert ("number" == type (value))
assert ((value - 0.0) < 0.00001)

-- String
value = Settings.get_string ("test-setting-string")
assert ("string" == type (value))
assert (value == "blahblah")
value = Settings.get_string ("test-setting-string2")
assert ("string" == type (value))
assert (value == "a string with \"quotes\"")
value = Settings.get_string ("test-setting-string-undefined")
assert ("string" == type (value))
assert (value == "")

-- Array
value = Settings.get_array ("test-setting-array")
assert (value[1] == 1)
assert (value[2] == 2)
assert (value[3] == 3)
assert (value[4] == nil)
assert (#value == 3)
value = Settings.get_array ("test-setting-array2")
assert (value[1] == "test1")
assert (value[2] == "test 2")
assert (value[3] == "test three")
assert (value[4] == "test-four")
assert (value[5] == nil)
assert (#value == 4)
value = Settings.get_array ("test-setting-array-undefined")
assert (next(value) == nil)

-- Object
value = Settings.get_object ("test-setting-object")
assert (value.key1 == "value")
assert (value.key2 == 2)
assert (value.key3 == true)
value = Settings.get_object ("test-setting-object-undefined")
assert (next(value) == nil)

-- Callbacks
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

  -- test #2
  setting = "test-setting-bool"
  setting_value = Json.Boolean (true)
  callback = false

  assert (Settings.set(setting, setting_value))
  assert (not callback)

  -- test #3
  setting = "test-setting-int"
  setting_value = Json.Int (99)
  callback = false

  assert (Settings.set(setting, setting_value))
  assert (callback)

  -- test #4
  setting = "test-setting-string"
  setting_value = Json.String ("lets not blabber")
  callback = false

  finish_activation = true
  assert (Settings.set(setting, setting_value))
  assert (callback)

end)
