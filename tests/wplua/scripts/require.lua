local testlib = require ("testlib")
assert (package.loaded ["testlib"] == testlib)

assert (type (testlib) == "table")

local x = 1
x = testlib.test_add_ten (x)
assert (x == 11)

local val_table = testlib.get_empty_table ("test-key")
assert (type (val_table) == "table")
val_table ["key"] = "value"
val_table.id = 100

testlib.set_table ("test-key", val_table)

local val1 = testlib.get_table ("test-key")
assert (val1 ["key"] == "value")
assert (val1.id == 100)
