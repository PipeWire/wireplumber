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
assert (val.width == 1920 and val.height == 1080)
assert (pod:get_type_name() == "Spa:Rectangle")

-- Fraction
pod = Pod.Fraction (3, 4)
val = pod:parse()
assert (val.num == 3 and val.denom == 4)
assert (pod:get_type_name() == "Spa:Fraction")

-- Struct
pod = Pod.Struct { true, 1, "string" }
val = pod:parse()
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
assert (val[1] == "Spa:Pod:Object:Param:PortConfig")
assert (val[2] == "PortConfig")
assert (val.direction == "Input")
assert (val.mode == "dsp")
assert (val.monitor)
assert (pod:get_type_name() == "Spa:Pod:Object:Param:PortConfig")

-- Sequence
pod = Pod.Sequence {
  {offset = 10, typename = "Properties", value = 1},
  {offset = 20, typename = "Properties", value = 2},
  {offset = 40, typename = "Properties", value = 4},
}
val = pod:parse()
assert (val[1].offset == 10 and val[1].typename == "Properties" and val[1].value == 1)
assert (val[2].offset == 20 and val[2].typename == "Properties" and val[2].value == 2)
assert (val[3].offset == 40 and val[3].typename == "Properties" and val[3].value == 4)
assert (pod:get_type_name() == "Spa:Pod:Sequence")

-- Array
pod = Pod.Array { "Spa:Bool", true, false, false, true }
val = pod:parse()
assert (val[1] == "Spa:Bool")
assert (val[2])
assert (not val[3])
assert (not val[4])
assert (val[5])
assert (pod:get_type_name() == "Spa:Array")

-- Choice
pod = Pod.Choice { "Enum", 5, 6, 7, 8 }
val = pod:parse()
assert (val[1] == "Enum")
assert (val[2] == 5)
assert (val[3] == 6)
assert (val[4] == 7)
assert (val[5] == 8)
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
    channels = 2,
    position = Pod.Array { "Spa:Id", 0, 1 }
  }
}
val = pod:parse()
assert (val[1] == "Spa:Pod:Object:Param:PortConfig")
assert (val[2] == "PortConfig")
assert (val.direction == "Input")
assert (val.mode == "dsp")
assert (val.monitor)
assert (val.format.mediaType == "audio")
assert (val.format.mediaSubtype == "raw")
assert (val.format.rate == 48000)
assert (val.format.channels == 2)
assert (val.format.position[1] == "Spa:Id" and val.format.position[2] == 0 and val.format.position[3] == 1)
assert (pod:get_type_name() == "Spa:Pod:Object:Param:PortConfig")
