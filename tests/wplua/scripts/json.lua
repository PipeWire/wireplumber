-- Null
json = Json.Null ()
assert (json:is_null())
assert (json:parse() == nil)
assert (json:get_data() == "null")
assert (json:get_size() == 4)
assert (json:get_data() == json:to_string())

-- Boolean
json = Json.Boolean (true)
assert (json:is_boolean())
assert (json:parse())
assert (json:get_data() == "true")
assert (json:get_size() == 4)
assert (json:get_data() == json:to_string())
json = Json.Boolean (false)
assert (json:is_boolean())
assert (not json:parse())
assert (json:get_data() == "false")
assert (json:get_size() == 5)
assert (json:get_data() == json:to_string())

-- Int
json = Json.Int (3)
assert (json:is_int())
assert (json:parse() == 3)
assert (json:get_data() == "3")
assert (json:get_size() == 1)
assert (json:get_data() == json:to_string())

-- Float
json = Json.Float(3.14)
assert (json:is_float())
val = json:parse ()
assert (val > 3.13 and val < 3.15)

-- String
json = Json.String ("wireplumber")
assert (json:is_string())
assert (json:parse() == "wireplumber")
assert (json:get_data() == "\"wireplumber\"")
assert (json:get_size() == 13)
assert (json:get_data() == json:to_string())

-- Array
json = Json.Array { Json.Null (), Json.Null () }
assert (json:is_array())
val = json:parse ()
assert (val[1] == nil)
assert (val[2] == nil)
assert (json:get_data() == "[null, null]")
assert (json:get_size() == 12)
assert (json:get_data() == json:to_string())

json = Json.Array { true, false }
assert (json:is_array())
val = json:parse ()
assert (val[1])
assert (not val[2])
assert (json:get_data() == "[true, false]")
assert (json:get_size() == 13)
assert (json:get_data() == json:to_string())

json = Json.Array {1, 2, 3}
assert (json:is_array())
val = json:parse ()
assert (val[1] == 1)
assert (val[2] == 2)
assert (val[3] == 3)
assert (json:get_data() == "[1, 2, 3]")
assert (json:get_size() == 9)
assert (json:get_data() == json:to_string())

json = Json.Array {1.11, 2.22, 3.33}
assert (json:is_array())
val = json:parse ()
assert (val[1] > 1.10 and val[1] < 1.12)
assert (val[2] > 2.21 and val[2] < 2.23)
assert (val[3] > 3.32 and val[3] < 3.34)

json = Json.Array {"lua", "spa", "json"}
assert (json:is_array())
val = json:parse ()
assert (val[1] == "lua")
assert (val[2] == "spa")
assert (val[3] == "json")
assert (val[4] == nil)
assert (json:get_data() == "[\"lua\", \"spa\", \"json\"]")
assert (json:get_size() == 22)
assert (json:get_data() == json:to_string())

json = Json.Array {}
assert (json:is_array())
val = json:parse ()
assert (#val == 0)
assert (json:get_data() == "[]")

json = Json.Array {
  Json.Array {
    Json.Object {
      key1 = 1
    },
    Json.Object {
      key2 = 2
    },
  }
}
assert (json:is_array())
assert (json:get_data() == "[[{\"key1\":1}, {\"key2\":2}]]")
assert (json:get_data() == json:to_string())
val = json:parse ()
assert (val[1][1].key1 == 1)
assert (val[1][2].key2 == 2)

table = {}
table[1] = 1
table[2] = 2
table[3] = 3
table["4"] = 4
json = Json.Array (table)
assert (json:is_array())
val = json:parse ()
assert (val[1] == 1)
assert (val[2] == 2)
assert (val[3] == 3)
assert (val["4"] == nil)

-- Object
json = Json.Object {
    key1 = Json.Null(),
    key2 = true,
    key3 = 3,
    key4 = 4.44,
    key5 = "foo",
    key6 = Json.Array {5, 6, 7},
    key7 = Json.Object {
      key_nested1 = "nested",
      key_nested2 = 8,
      key_nested3 = Json.Array {false, true, false},
      ["Key with spaces and (special % characters)"] = 50.0,
    }
}
assert (json:is_object())
val = json:parse ()
assert (val.key1 == nil)
assert (val.key2 == true)
assert (val.key3 == 3)
assert (val.key4 > 4.43 and val.key4 < 4.45)
assert (val.key5 == "foo")
assert (val.key6[1] == 5)
assert (val.key6[2] == 6)
assert (val.key6[3] == 7)
assert (val.key7.key_nested1 == "nested")
assert (val.key7.key_nested2 == 8)
assert (not val.key7.key_nested3[1])
assert (val.key7.key_nested3[2])
assert (not val.key7.key_nested3[3])
assert (val.key7["Key with spaces and (special % characters)"] == 50.0)

table = {}
table["1"] = 1
table["2"] = 2
table["3"] = 3
table[4] = 4
json = Json.Object (table)
assert (json:is_object())
val = json:parse ()
assert (val["1"] == 1)
assert (val["2"] == 2)
assert (val["3"] == 3)
assert (val[4] == nil)

json = Json.Object {}
assert (json:is_object())
val = json:parse ()
assert (#val == 0)
assert (val.key1 == nil)
assert (json:get_data() == "{}")

-- Raw
json = Json.Raw ("[\"foo\", \"bar\"]")
assert (json:is_array())
assert (json:get_data() == "[\"foo\", \"bar\"]")
assert (json:get_data() == json:to_string())
val = json:parse ()
assert (val[1] == "foo")
assert (val[2] == "bar")

json = Json.Raw ("[foo, bar]")
assert (json:is_array())
assert (json:get_data() == "[foo, bar]")
assert (json:get_data() == json:to_string())
val = json:parse ()
assert (val[1] == "foo")
assert (val[2] == "bar")

json = Json.Raw ("{\"name\": \"wireplumber\", \"version\": [0, 4, 7]}")
assert (json:is_object())
assert (json:get_data() == "{\"name\": \"wireplumber\", \"version\": [0, 4, 7]}")
assert (json:get_data() == json:to_string())
val = json:parse ()
assert (val.name == "wireplumber")
assert (val.version[1] == 0)
assert (val.version[2] == 4)
assert (val.version[3] == 7)

-- recursion limit
json = Json.Raw ("{ name = wireplumber, version = [0, 4, 15], args = { test = [0, 1] } }")

val = json:parse (0)
assert (type (val) == "string")
assert (val == "{ name = wireplumber, version = [0, 4, 15], args = { test = [0, 1] } }")

val = json:parse (1)
assert (type (val) == "table")
assert (val.name == "wireplumber")
assert (type (val.version) == "string")
assert (val.version == "[0, 4, 15]")
assert (type (val.args) == "string")
assert (val.args == "{ test = [0, 1] }")

val = json:parse(2)
assert (type (val) == "table")
assert (val.name == "wireplumber")
assert (type (val.version) == "table")
assert (val.version[1] == 0)
assert (val.version[2] == 4)
assert (val.version[3] == 15)
assert (type (val.args) == "table")
assert (val.args.test == "[0, 1]")

val = json:parse(3)
assert (type (val) == "table")
assert (val.name == "wireplumber")
assert (type (val.version) == "table")
assert (val.version[1] == 0)
assert (val.version[2] == 4)
assert (val.version[3] == 15)
assert (type (val.args) == "table")
assert (type (val.args.test) == "table")
assert (val.args.test[1] == 0)
assert (val.args.test[2] == 1)

json = Json.Array { "foo" }
json2 = Json.Array { "bar" }
json = json:merge(json2)
assert (json:is_array())
val = json:parse ()
assert (val[1] == "foo")
assert (val[2] == "bar")

table = {}
table["1"] = 1
table["2"] = 2
json = Json.Object (table)
table = {}
table["3"] = 3
table["4"] = 4
json2 = Json.Object (table)
json = json:merge(json2)
val = json:parse ()
assert (val["1"] == 1)
assert (val["2"] == 2)
assert (val["3"] == 3)
assert (val["4"] == 4)
