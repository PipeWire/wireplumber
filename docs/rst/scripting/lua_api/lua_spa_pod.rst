.. _lua_spa_pod:

Spa Pod
=======

``Pod`` binds the :ref:`WpSpaPod <spa_pod_api>` C API. Pods are SPA's
serialization format for typed values; they are what PipeWire object *params*
are made of. Scripts encounter them whenever they read or write a param —
``Props``, ``Format``, ``Route``, ``Profile``, ``PortConfig`` and so on.

.. code-block:: lua

   -- read the Props param of a node
   for p in node:iterate_params ("Props") do
     local props = p:parse ()
     log:info ("volume: " .. tostring (props.volume))
   end

.. note::

   The shipped scripts usually go through ``common-utils``'
   ``parseParam (pod, "Props")`` instead of calling :func:`Pod.parse` directly;
   it parses the pod and verifies that it really is of the expected object
   type, returning nil otherwise.

Building a pod and setting it as a param:

.. code-block:: lua

   local param = Pod.Object {
     "Spa:Pod:Object:Param:Route", "Route",
     index = route_index,
     device = device_id,
     props = Pod.Object {
       "Spa:Pod:Object:Param:Props", "Route",
       mute = false,
       channelVolumes = Pod.Array { "Spa:Float", 0.5, 0.5 },
     },
     save = false,
   }

   device:set_param ("Route", param)

Methods
-------

.. function:: Pod.parse(self)

   Converts the pod into the equivalent Lua value. Primitives become Lua
   booleans, numbers or strings; containers become tables.

   For an object pod, the resulting table has the object's property names as
   keys, plus two extra entries: ``pod_type`` (the string ``"Object"``) and
   ``object_id``.

   :returns: the converted value

.. function:: Pod.get_type_name(self)

   :returns: the name of the SPA type of this pod, e.g.
      ``"Spa:Pod:Object:Param:Props"``
   :rtype: string

.. function:: Pod.fixate(self)

   Binds :c:func:`wp_spa_pod_fixate`

   Fixates a choice pod, i.e. reduces it to its default value.

   :returns: true if the pod was fixated
   :rtype: boolean

.. function:: Pod.filter(self, filter)

   Binds :c:func:`wp_spa_pod_filter`

   Returns the intersection of this pod and *filter*. Used to negotiate a
   format that satisfies both sides.

   :param Pod filter: the pod to intersect with; if nil, a copy of *self* is
      returned
   :returns: the intersection, or nil if it could not be made
   :rtype: Pod

Primitive constructors
----------------------

.. function:: Pod.None()
.. function:: Pod.Boolean(value)
.. function:: Pod.Id(value)
.. function:: Pod.Int(value)
.. function:: Pod.Long(value)
.. function:: Pod.Float(value)
.. function:: Pod.Double(value)
.. function:: Pod.String(value)
.. function:: Pod.Bytes(value)
.. function:: Pod.Pointer(type_name, value)
.. function:: Pod.Fd(value)

   Construct a pod holding a single value of the corresponding SPA type.

.. function:: Pod.Rectangle(width, height)

   Constructs a rectangle pod.

   :param integer width: the width
   :param integer height: the height

.. function:: Pod.Fraction(num, denom)

   Constructs a fraction pod.

   :param integer num: the numerator
   :param integer denom: the denominator

Container constructors
----------------------

.. function:: Pod.Object(decl)

   Constructs an object pod. The table's first two array entries are the SPA
   type name of the object and the id name; the remaining string-keyed entries
   are the object's properties.

   .. code-block:: lua

      Pod.Object {
        "Spa:Pod:Object:Param:Props", "Props",
        mute = false,
        volume = 0.5,
      }

   The property names and their types come from the SPA type information for
   the object type, so only names that the type actually defines can be used.

.. function:: Pod.Struct(decl)

   Constructs a struct pod from the array entries of the table. Unlike arrays
   and choices, a struct is heterogeneous, so there is no type name entry;
   every entry is a value and they are stored in the order they appear.
   Entries may be Lua booleans, numbers, strings, or other pods — which is how
   a struct can nest other containers.

   .. code-block:: lua

      Pod.Struct {
        "hello", 42, 0.5, true,
        Pod.Rectangle (1920, 1080),
        Pod.Struct { "inner", 1 },
      }

   Plain Lua numbers are stored as the widest matching type: integers become
   ``Spa:Long`` and non-integers become ``Spa:Double``. If the receiving side
   expects a specific type, construct it explicitly:

   .. code-block:: lua

      Pod.Struct { Pod.Int (42), Pod.Float (0.5) }

.. function:: Pod.Array(decl)

   Constructs an array pod. Like the choice constructors below, the table's
   first entry is the type of the values and all the remaining entries are the
   values, which must all be of that type.

   .. code-block:: lua

      Pod.Array { "Spa:Float", 0.5, 0.5 }
      Pod.Array { "Spa:Enum:AudioChannel", "FL", "FR" }

.. function:: Pod.Sequence(decl)

   Constructs a sequence pod, i.e. a list of timed controls. Each entry is
   itself a table and, contrary to the other containers, its fields are named
   rather than positional:

   ``offset``
     the media offset at which the control takes effect, relative to the start
     of the buffer that carries the sequence — for audio, this is a number of
     samples; defaults to 0 if omitted
   ``typename``
     the kind of control, a short name of the ``Spa:Enum:Control`` id table,
     i.e. ``"Properties"``, ``"Midi"`` or ``"OSC"``
   ``value``
     the payload of the control, whose expected type depends on ``typename``;
     it may be a Lua boolean, number or string, or another pod. A
     ``"Properties"`` control carries a ``Props`` object pod, while ``"Midi"``
     and ``"OSC"`` controls carry raw bytes.

   .. code-block:: lua

      -- a volume ramp: full volume at the start of the buffer,
      -- half volume 4800 samples later
      Pod.Sequence {
        { offset = 0, typename = "Properties",
          value = Pod.Object { "Spa:Pod:Object:Param:Props", "Props",
                               volume = 1.0 } },
        { offset = 4800, typename = "Properties",
          value = Pod.Object { "Spa:Pod:Object:Param:Props", "Props",
                               volume = 0.5 } },
      }

   Entries that do not specify both a ``typename`` and a ``value`` are silently
   dropped from the resulting sequence.

Choice constructors
-------------------

Choice pods describe a set of acceptable values rather than a single one; they
are what format negotiation is expressed in.

All the choice constructors take a single table, whose entries are **purely
positional**. There are no string keys and the order is not interchangeable:
the same values in a different order describe a different — or an invalid —
choice. The general form is:

.. code-block:: text

   Pod.Choice.<Kind> { <value type>, <default>, <more values...> }

1. The **first** entry is always a string with the type of the values, i.e.
   either a primitive SPA type name (``"Spa:Bool"``, ``"Spa:Id"``,
   ``"Spa:Int"``, ``"Spa:Long"``, ``"Spa:Float"``, ``"Spa:Double"``,
   ``"Spa:Fd"``) or the name of an id table, such as
   ``"Spa:Enum:AudioFormat"``. In the latter case the values may be written as
   the short names of that table (``"S16LE"``, ``"FL"``, ...). Strings and byte
   arrays cannot be used in a choice.
2. The **second** entry is always the default value, i.e. the value that
   :func:`Pod.fixate` reduces the choice to.
3. The remaining entries depend on the kind of choice and are described below.
   Their meaning is derived from their position, so none of them may be
   omitted.

.. function:: Pod.Choice.None(decl)

   A choice with a single possible value:
   ``{ <value type>, <value> }``

   .. code-block:: lua

      Pod.Choice.None { "Spa:Bool", false }

.. function:: Pod.Choice.Range(decl)

   A range of values, given as default, minimum and maximum, in this order:
   ``{ <value type>, <default>, <min>, <max> }``

   .. code-block:: lua

      Pod.Choice.Range { "Spa:Int", 44100, 8000, 192000 }

.. function:: Pod.Choice.Step(decl)

   A range with a step, given as default, minimum, maximum and step, in this
   order: ``{ <value type>, <default>, <min>, <max>, <step> }``

   .. code-block:: lua

      Pod.Choice.Step { "Spa:Int", 1024, 256, 8192, 256 }

.. function:: Pod.Choice.Enum(decl)

   An enumeration: the default, followed by the list of acceptable values:
   ``{ <value type>, <default>, <value 1>, <value 2>, ... }``

   .. code-block:: lua

      Pod.Choice.Enum { "Spa:Enum:AudioFormat", "S16LE", "S16LE", "F32LE" }

   The default is **not** implicitly part of the enumeration; it is only the
   preferred value. If it is also meant to be acceptable — which is almost
   always the case — it must be repeated as the first of the values, as in the
   example above, which reads *"S16LE preferred, S16LE or F32LE acceptable"*.

   For ``"Spa:Bool"``, this is enforced: the table must contain exactly three
   values, the first two being equal and the third one being the opposite, e.g.
   ``Pod.Choice.Enum { "Spa:Bool", true, true, false }``.

.. function:: Pod.Choice.Flags(decl)

   A set of flags that may be combined: the default combination, followed by
   the flags that may be set:
   ``{ <value type>, <default>, <flag 1>, <flag 2>, ... }``

   .. code-block:: lua

      Pod.Choice.Flags { "Spa:Int", 1 << 0, 1 << 0, 1 << 2, 1 << 3 }
