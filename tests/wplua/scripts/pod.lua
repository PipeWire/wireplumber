-- None
pod = Pod.None ()
assert (pod:parse() == nil)
assert (pod:get_type_name() == "Spa:None")

-- Bool
pod = Pod.Boolean (true)
assert (pod:parse())
assert (pod:get_type_name() == "Spa:Bool")

-- Id
pod = Pod.Id (4)
assert (pod:parse() == 4)
assert (pod:get_type_name() == "Spa:Id")
pod = Pod.Id("Spa:Enum:AudioChannel", "FL")
assert (pod:parse() == 3)
assert (pod:get_type_name() == "Spa:Id")

-- Int
pod = Pod.Int (64)
assert (pod:parse() == 64)
assert (pod:get_type_name() == "Spa:Int")

-- Long
pod = Pod.Long (1024)
assert (pod:parse() == 1024)
assert (pod:get_type_name() == "Spa:Long")

-- Float
pod = Pod.Float (1.02)
val = pod:parse ()
assert (val > 1.01 and val < 1.03)
assert (pod:get_type_name() == "Spa:Float")

-- Double
pod = Pod.Double (3.14159265359)
val = pod:parse ()
assert (val > 3.14159265358 and val < 3.14159265360)
assert (pod:get_type_name() == "Spa:Double")

-- String
pod = Pod.String ("a pod string")
assert (pod:parse() == "a pod string")
assert (pod:get_type_name() == "Spa:String")

-- Bytes
pod = Pod.Bytes ("16characterbytes")
assert (pod:parse() == "16characterbytes")
assert (pod:get_type_name() == "Spa:Bytes")

-- Pointer
pod = Pod.Pointer ("Spa:Pointer:Buffer", nil)
assert (pod:parse() == nil)
assert (pod:get_type_name() == "Spa:Pointer:Buffer")

-- Fd
pod = Pod.Fd (5)
assert (pod:parse() == 5)
assert (pod:get_type_name() == "Spa:Fd")

-- Rectangle
pod = Pod.Rectangle (1920, 1080)
val = pod:parse()
assert (val.pod_type == "Rectangle")
assert (val.width == 1920 and val.height == 1080)
assert (pod:get_type_name() == "Spa:Rectangle")

-- Fraction
pod = Pod.Fraction (3, 4)
val = pod:parse()
assert (val.pod_type == "Fraction")
assert (val.num == 3 and val.denom == 4)
assert (pod:get_type_name() == "Spa:Fraction")

-- Struct
pod = Pod.Struct { true, 1, "string" }
val = pod:parse()
assert (val.pod_type == "Struct")
assert (val[1] and val[2] == 1 and val[3] == "string")
assert (pod:get_type_name() == "Spa:Pod:Struct")

-- Object
pod = Pod.Object {
  "Spa:Pod:Object:Param:PortConfig", "PortConfig",
  direction = "Input",
  mode = "dsp",
  monitor = true
}
val = pod:parse()
assert (val.pod_type == "Object")
assert (val.id_type == "PortConfig")
assert (val.properties.direction == "Input")
assert (val.properties.mode == "dsp")
assert (val.properties.monitor)
assert (pod:get_type_name() == "Spa:Pod:Object:Param:PortConfig")

-- Sequence
pod = Pod.Sequence {
  {offset = 10, typename = "Properties", value = 1},
  {offset = 20, typename = "Properties", value = 2},
  {offset = 40, typename = "Properties", value = 4},
}
val = pod:parse()
assert (val.pod_type == "Sequence")
assert (val[1].offset == 10 and val[1].typename == "Properties" and val[1].value == 1)
assert (val[2].offset == 20 and val[2].typename == "Properties" and val[2].value == 2)
assert (val[3].offset == 40 and val[3].typename == "Properties" and val[3].value == 4)
assert (pod:get_type_name() == "Spa:Pod:Sequence")

-- Array
pod = Pod.Array { "Spa:Bool", true, false, false, true }
val = pod:parse()
assert (val.pod_type == "Array")
assert (val.value_type == "Spa:Bool")
assert (val[1])
assert (not val[2])
assert (not val[3])
assert (val[4])
assert (pod:get_type_name() == "Spa:Array")
pod = Pod.Array { "Spa:Enum:AudioChannel", "FL", "FR" }
val = pod:parse()
assert (val.pod_type == "Array")
assert (val.value_type == "Spa:Id")
assert (val[1] == 3)
assert (val[2] == 4)

-- Choice
pod = Pod.Choice.None { "Spa:Bool", false }
val = pod:parse()
assert(val.pod_type == "Choice.None")
assert(val.value_type == "Spa:Bool")
assert(not val[1])
pod = Pod.Choice.Range { "Spa:Float", 1.0, 0.0, 1.0 }
val = pod:parse()
assert (pod:get_type_name() == "Spa:Pod:Choice")
assert(val.pod_type == "Choice.Range")
assert(val.value_type == "Spa:Float")
assert(val[1] == 1.0)
assert(val[2] == 0.0)
assert(val[3] == 1.0)
assert (pod:get_type_name() == "Spa:Pod:Choice")
pod = Pod.Choice.Step { "Spa:Int", 5, 10 }
val = pod:parse()
assert(val.pod_type == "Choice.Step")
assert(val.value_type == "Spa:Int")
assert(val[1] == 5)
assert(val[2] == 10)
assert (pod:get_type_name() == "Spa:Pod:Choice")
pod = Pod.Choice.Enum { "Spa:Enum:AudioChannel", "FL", "FL", "FR" }
val = pod:parse()
assert (val.pod_type == "Choice.Enum")
assert (val.value_type == "Spa:Id")
assert (val[1] == 3)
assert (val[2] == 3)
assert (val[3] == 4)
assert (pod:get_type_name() == "Spa:Pod:Choice")
pod = Pod.Choice.Flags { "Spa:Int", 1 << 0, 1 << 2, 1 << 3}
val = pod:parse()
assert (val.pod_type == "Choice.Flags")
assert (val.value_type == "Spa:Int")
assert (val[1] == 1 << 0)
assert (val[2] == 1 << 2)
assert (val[3] == 1 << 3)
assert (pod:get_type_name() == "Spa:Pod:Choice")


-- Nested Pods
pod = Pod.Object {
  "Spa:Pod:Object:Param:PortConfig", "PortConfig",
  direction = "Input",
  mode = "dsp",
  monitor = true,
  format = Pod.Object {
    "Spa:Pod:Object:Param:Format", "Format",
    mediaType = "audio",
    mediaSubtype = "raw",
    rate = 48000,
    channels = Pod.Choice.Range { "Spa:Int", 2, 1, 32 },
    position = Pod.Array { "Spa:Enum:AudioChannel", "FL", "FR" }
  }
}
val = pod:parse()
assert (val.pod_type == "Object")
assert (val.id_type == "PortConfig")
assert (val.properties.direction == "Input")
assert (val.properties.mode == "dsp")
assert (val.properties.monitor)
assert (val.properties.format.pod_type == "Object")
assert (val.properties.format.id_type == "Format")
assert (val.properties.format.properties.mediaType == "audio")
assert (val.properties.format.properties.mediaSubtype == "raw")
assert (val.properties.format.properties.rate == 48000)
assert (val.properties.format.properties.channels.pod_type == "Choice.Range")
assert (val.properties.format.properties.channels.value_type == "Spa:Int")
assert (val.properties.format.properties.channels[1] == 2)
assert (val.properties.format.properties.channels[2] == 1)
assert (val.properties.format.properties.channels[3] == 32)
assert (val.properties.format.properties.position.pod_type == "Array")
assert (val.properties.format.properties.position.value_type == "Spa:Id")
assert (val.properties.format.properties.position[1] == 3 and val.properties.format.properties.position[2] == 4)
assert (pod:get_type_name() == "Spa:Pod:Object:Param:PortConfig")
