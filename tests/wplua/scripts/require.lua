local testlib = require("testlib")

assert(type(testlib) == "table")
assert(package.loaded["testlib"] == testlib)

local x = 1
x = testlib.test_add_ten(x)
assert(x == 11)
