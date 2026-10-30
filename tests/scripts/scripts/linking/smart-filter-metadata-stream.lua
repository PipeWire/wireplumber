-- Tests that a filter metadata key with a port group applies only to the stream
-- with that port group, and has priority over the key without port group.

local futils = require ("filter-utils")

Script.async_activation = true

local function fakeStream (node_id, port_group)
  return {
    get_node_bound_id = function () return node_id end,
    get_port_group = function () return port_group end,
  }
end

filters_metadata = ImplMetadata ("filters")
filters_metadata:activate (Features.ALL, function (m, e)
  assert (e == nil, tostring (e))

  local key = "filter.smart.disabled"
  local stream_a = fakeStream (10, "a")
  local stream_b = fakeStream (10, "b")
  local stream_none = fakeStream (10, nil)

  m:set (10, key .. ".a", "Spa:String:JSON", "true")
  assert (futils.get_metadata_value (m, stream_a, key) == "true")
  assert (futils.get_metadata_value (m, stream_b, key) == nil)
  assert (futils.get_metadata_value (m, stream_none, key) == nil)

  m:set (10, key, "Spa:String:JSON", "false")
  assert (futils.get_metadata_value (m, stream_a, key) == "true")
  assert (futils.get_metadata_value (m, stream_b, key) == "false")
  assert (futils.get_metadata_value (m, stream_none, key) == "false")

  assert (futils.get_metadata_value (m, fakeStream (11, "a"), key) == nil)

  Script:finish_activation ()
end)
