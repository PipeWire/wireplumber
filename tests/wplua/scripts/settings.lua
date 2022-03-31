
--  tests the lua API of WpSettings, this file tests the settings present in
--  settings.conf

assert (Settings.get_boolean("property1", "test-settings") == false)
assert (Settings.get_boolean("property2", "test-settings") == true)

assert (Settings.get_boolean("property1") == false)
