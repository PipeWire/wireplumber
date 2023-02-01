local settings_manager = require ("settings-manager")

local defaults = {
  ["test-setting1"] = true,
  ["test-setting2"] = true,
  ["test-setting2-1"] = false,
  ["test-setting3-int"] = -10,
  ["test-setting4-string"] = "blahblahblah",
  ["test-setting5-string-with-quotes"] = "a string with out \"quotes\"",
}

local test_settings = settings_manager.new ("test.", defaults)

assert (test_settings.test_setting1 == false)
assert (test_settings.test_setting2 == true)
assert (test_settings.test_setting2_1 == false)
assert (test_settings.test_setting3_int == -20)
assert (test_settings.test_setting4_string == "blahblah")

local setting
local setting_value
local callback
local finish_activation

function handleSettingsChange (value)
  callback = true
  assert (value == setting_value:parse ())
end

function handleSettingsChange1 (value)
  if (finish_activation) then
    Script:finish_activation ()
  end
end

test_settings:subscribe ("test-setting1", handleSettingsChange)
test_settings:subscribe ("test-setting3-int", handleSettingsChange)
test_settings:subscribe ("test-setting4-string", handleSettingsChange)
test_settings:subscribe ("test-setting4-string", handleSettingsChange1)

metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "sm-settings" },
  }
}
metadata_om:activate ()

metadata_om:connect ("objects-changed", function (om)
  local metadata = om:lookup ()

  if (not metadata) then
    return
  end

  -- test #1
  setting = "test.test-setting1"
  setting_value = Json.Boolean (true)
  callback = false

  metadata:set (0, setting, "Spa:String:JSON", setting_value:get_data ())

  -- test #2
  setting = "test.test-setting1"
  setting_value = Json.Boolean (true)
  callback = false

  metadata:set (0, setting, "Spa:String:JSON", setting_value:get_data ())
  assert (not callback)

  -- test #3
  setting = "test.test-setting3-int"
  setting_value = Json.Int (99)
  callback = false

  metadata:set (0, setting, "Spa:String:JSON", setting_value:get_data ())
  assert (callback)

  -- test #4
  setting = "test.test-setting4-string"
  setting_value = Json.String ("lets not blabber")
  callback = false

  finish_activation = true
  metadata:set (0, setting, "Spa:String:JSON", setting_value:get_data ())
  assert (callback)
end)
