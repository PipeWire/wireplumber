
--  tests the lua API of WpSettings, this file tests the settings present in
--  settings.conf

assert (Settings.get_boolean("test-property1", "test-settings") == false)
assert (Settings.get_boolean("test-property2", "test-settings") == true)

assert (Settings.get_boolean("test-property1") == false)
