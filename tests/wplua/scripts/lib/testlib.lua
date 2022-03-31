local testlib = {}

function testlib.test_add_ten (x)
  return x + 10
end

Log.info("in testlib")

return testlib
