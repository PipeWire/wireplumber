.. _lua_json_api:

Json
====

``Json`` binds the :ref:`WpSpaJson <spa_json_api>` C API, which represents
values in SPA-JSON — the format WirePlumber's configuration files are written
in. Scripts encounter it whenever they read a configuration section with
:func:`Conf.get_section_as_json` or a setting with :func:`Settings.get`.

``JsonUtils`` provides the *rules* matching engine on top of it, which is what
makes ``*.rules`` configuration sections work.

Constructors
------------

.. function:: Json.Null()
.. function:: Json.Boolean(value)
.. function:: Json.Int(value)
.. function:: Json.Float(value)
.. function:: Json.String(value)

   Construct a JSON value of the corresponding primitive type.

.. function:: Json.Array(table)

   Constructs a JSON array from a Lua table. Only the integer-keyed entries of
   the table are used. Values may be booleans, numbers, strings or other
   ``Json`` objects.

   .. code-block:: lua

      local empty = Json.Array {}
      local list = Json.Array { "a", "b", "c" }

.. function:: Json.Object(value)

   Constructs a JSON object either from a Lua table, using only its
   string-keyed entries, or from a :ref:`Properties <lua_properties_api>`
   object.

   .. code-block:: lua

      local obj = Json.Object { ["node.name"] = "my-node", ["priority.session"] = 1000 }

.. function:: Json.Raw(string)

   Constructs a JSON value by parsing a SPA-JSON string directly.

   :param string string: the SPA-JSON text to parse

Inspecting values
-----------------

.. function:: Json.is_null(self)
.. function:: Json.is_boolean(self)
.. function:: Json.is_int(self)
.. function:: Json.is_float(self)
.. function:: Json.is_string(self)
.. function:: Json.is_array(self)
.. function:: Json.is_object(self)

   Test the type of the value.

   :returns: true if the value is of the corresponding type
   :rtype: boolean

.. function:: Json.parse(self, n_recursions)

   Converts the JSON value into the equivalent Lua value: a boolean, number or
   string for primitives, or a table for arrays and objects.

   :param integer n_recursions: *(optional)* how many levels deep to convert
      nested containers; by default the whole value is converted
   :returns: the converted value

.. function:: Json.to_string(self)

   Returns the SPA-JSON text of this value, and nothing else.

   :returns: the SPA-JSON text representation of the value
   :rtype: string

.. function:: Json.get_size(self)

   :returns: the size in bytes of the SPA-JSON text representation of the value
   :rtype: integer

.. function:: Json.merge(self, other)

   Merges two JSON containers. Both values must be arrays, or both must be
   objects; merging anything else raises an error.

   :param Json other: the value to merge into a copy of *self*
   :returns: the merged value
   :rtype: Json

.. _lua_json_api_rules:

JsonUtils — matching rules
--------------------------

A *rules* configuration section is an array of objects, each with a ``matches``
list of conditions and an ``actions`` object describing what to do when a
condition matches. This is the mechanism behind ``monitor.alsa.rules``,
``monitor.v4l2.rules``, ``stream.rules`` and the other ``*.rules`` sections.

.. function:: JsonUtils.match_rules_update_properties(rules, properties)

   Binds :c:func:`wp_json_utils_match_rules_update_properties`

   Applies the ``update-props`` action of every matching rule to *properties*.
   This is by far the most common use of the rules engine: a monitor reads its
   rules section once, then runs every object's properties through this
   function before creating the object.

   .. code-block:: lua

      config.rules = Conf.get_section_as_json ("monitor.bluez.rules", Json.Array {})
      -- ...
      properties = JsonUtils.match_rules_update_properties (config.rules, properties)

   :param Json rules: the rules array
   :param properties: a table or :ref:`Properties <lua_properties_api>` object
      to match against and update
   :returns: the updated properties, and the number of properties that were
      changed
   :rtype: Properties, integer

.. function:: JsonUtils.match_rules(rules, properties, callback)

   Binds :c:func:`wp_json_utils_match_rules`

   The general form: for every rule whose ``matches`` conditions are satisfied
   by *properties*, calls *callback* with the action name and its value. Use
   this when the rules define actions other than ``update-props``.

   :param Json rules: the rules array
   :param properties: a table or :ref:`Properties <lua_properties_api>` object
      to match against
   :param function callback: called as ``callback(action, value)`` for each
      matching action
   :returns: true if the rules were processed successfully, plus an error
      message string if not
   :rtype: boolean, string
