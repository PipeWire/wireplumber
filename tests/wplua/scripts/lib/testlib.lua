local testlib = {}

testlib.table1 = {}

testlib.table1 ["test-key"] = "test-value"

function testlib.test_add_ten (x)
  return x + 10
end

function testlib.get_empty_table (key)
  testlib.table1 [key] = {}
  return testlib.table1 [key]
end

function testlib.set_table (key, value)
  testlib.table1 [key] = value
end

function testlib.get_table (key)
  return testlib.table1 [key]
end

Log.info ("in testlib")

return testlib
