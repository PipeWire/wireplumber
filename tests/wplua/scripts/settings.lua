
--  tests the lua API of WpSettings, this file tests the settings present in
--  settings.conf

assert (Settings.get_setting_for_boolean("test-settings","property1") == false)
assert (Settings.get_setting_for_boolean("test-settings","property2") == true)
