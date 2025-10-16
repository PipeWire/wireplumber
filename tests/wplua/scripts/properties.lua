-- create empty properties
props = Properties ()

-- set nil
props["key-nil"] = nil
assert (props["key-nil"] == nil)

-- set bool
props["key-bool"] = false
assert (props["key-bool"] == "false")
assert (props:get_boolean ("key-bool") == false)
props["key-bool"] = true
assert (props["key-bool"] == "true")
assert (props:get_boolean ("key-bool") == true)

-- set int
props["key-int"] = 4
assert (props["key-int"] == "4")
assert (props:get_int ("key-int") == 4)

-- set float
props["key-float"] = 3.14
val = props:get_float ("key-float")
assert (val > 3.13 and val < 3.15)

-- set string
props["key-string"] = "value"
assert (props["key-string"] == "value")
assert (props:get_boolean ("key-string") == false)
assert (props:get_int ("key-string") == nil)
assert (props:get_float ("key-string") == nil)

-- copy
copy = props:copy ()
assert (copy["key-nil"] == nil)
assert (copy:get_boolean ("key-bool") == true)
assert (copy:get_int ("key-int") == 4)
val = copy:get_float ("key-float")
assert (val > 3.13 and val < 3.15)
assert (copy["key-string"] == "value")

-- remove int property
props["key-int"] = nil
assert (props["key-int"] == nil)
assert (copy:get_int ("key-int") == 4)

-- create properties from table
props = Properties {
  ["key0"] = nil,
  ["key1"] = false,
  ["key2"] = 64,
  ["key3"] = 2.71,
  ["key4"] = "string",
}
assert (props["key0"] == nil)
assert (props:get_boolean ("key1") == false)
assert (props:get_int ("key2") == 64)
val = props:get_float ("key3")
assert (val > 2.70 and val < 2.72)
assert (props["key4"] == "string")

-- count
assert (props:get_count () == 4)

-- parse
parsed = props:parse ()
assert (parsed["key0"] == nil)
assert (parsed["key1"] == "false")
assert (tonumber (parsed["key2"]) == 64)
val = tonumber (parsed["key3"])
assert (val > 2.70 and val < 2.72)
assert (parsed["key4"] == "string")

-- pairs
values = {}
for k, v in pairs (props) do
  values [k] = v
end
assert (values["key0"] == nil)
assert (values["key1"] == "false")
assert (tonumber (values["key2"]) == 64)
val = tonumber (values["key3"])
assert (val > 2.70 and val < 2.72)
assert (values["key4"] == "string")

-- Make sure the reference changes are also updated
local properties = Properties ()
properties["key"] = "value"
assert (properties["key"] == "value")
local properties2 = properties
properties2["key"] = "another-value"
assert (properties2["key"] == "another-value")
assert (properties["key"] == "another-value")
