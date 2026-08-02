.. _lua_properties_api:

Properties
==========

``Properties`` is a string-to-string dictionary, the type used throughout
WirePlumber for PipeWire object properties, event properties and so on. It
binds the :ref:`WpProperties <properties_api>` C API.

Most of the time scripts obtain a ``Properties`` object from somewhere else —
``node.properties``, :func:`Event.get_properties`,
:func:`Conf.get_section_as_properties` — rather than constructing one.

Accessing values
----------------

Values are read and written with the normal Lua indexing syntax. Reading a key
that is not set returns nil.

.. code-block:: lua

   local props = node.properties

   local name = props ["node.name"]
   props ["my.custom.property"] = "value"

Because a ``Properties`` object holds strings, the typed accessors below are
provided for the common cases of reading a value that represents a boolean or
a number.

.. function:: Properties(value)

   Constructs a new ``Properties`` object.

   :param value: a Lua table of string keys and values to initialize from, or
      another ``Properties`` object to copy, or nothing for an empty object
   :returns: the new properties object
   :rtype: Properties

.. function:: Properties.get_boolean(self, key)

   Returns the value of *key* interpreted as a boolean.

   :param string key: the key to look up
   :returns: the value, or nil if the key is not set
   :rtype: boolean

.. function:: Properties.get_int(self, key)

   Returns the value of *key* interpreted as an integer.

   :param string key: the key to look up
   :returns: the value, or nil if the key is not set
   :rtype: integer

.. function:: Properties.get_float(self, key)

   Returns the value of *key* interpreted as a floating point number.

   :param string key: the key to look up
   :returns: the value, or nil if the key is not set
   :rtype: number

.. function:: Properties.get_count(self)

   Binds :c:func:`wp_properties_get_count`

   :returns: the number of properties in the object
   :rtype: integer

.. function:: Properties.copy(self)

   Binds :c:func:`wp_properties_copy`

   Returns an independent copy of the properties, which can be modified without
   affecting the original.

   :returns: a copy of the properties
   :rtype: Properties

.. function:: Properties.parse(self)

   Converts the properties into a plain Lua table of string keys and values.

   .. note::

      This creates a full copy. Prefer indexing the ``Properties`` object
      directly when you only need a few keys; the shipped scripts were
      converted to do so because building a table for every object turned out
      to be measurably expensive.

   :returns: a table with the contents of the properties
   :rtype: table
