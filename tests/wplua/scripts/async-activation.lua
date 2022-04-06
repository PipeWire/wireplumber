Script.async_activation = true

tags = {}

function checkpoint(x)
  Log.info(x)
  table.insert(tags, x)
end

Core.timeout_add(100, function()
  checkpoint("timeout1")
  return false
end)

Core.timeout_add(200, function()
  checkpoint("timeout2")
  return false
end)

Core.timeout_add(300, function()
  checkpoint("timeout3")
  assert(#tags == 3)
  assert(tags[1] == "timeout1")
  assert(tags[2] == "timeout2")
  assert(tags[3] == "timeout3")
  Script:finish_activation()
  return false
end)

assert(#tags == 0)
