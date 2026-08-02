.. _lua_conf_api:

Conf
====

The ``Conf`` API gives scripts access to the static configuration — the merged
contents of ``wireplumber.conf`` and all of its fragment files. It binds the
:ref:`WpConf <conf_api>` C API.

This is how scripts read their own configuration sections. Almost every shipped
script starts by reading the section it cares about, for example::

   config.properties = Conf.get_section_as_properties ("monitor.bluez.properties")
   config.rules = Conf.get_section_as_json ("monitor.bluez.rules", Json.Array {})

See :ref:`config_conf_file` for how the configuration file and its fragments
are located and merged, and :ref:`config_modifying_configuration` for how users
are expected to override these sections.

.. note::

   Unlike :ref:`Settings <lua_settings_api>`, the static configuration is read
   once at startup and does not change while WirePlumber is running. Use
   settings for anything the user should be able to change at runtime.

Reading sections
----------------

All the ``get_section_*`` functions operate on the core's configuration by
default and can be called as static functions. They can also be called as
methods on a ``Conf`` object, in which case that object's configuration is used
instead.

.. function:: Conf.get_section_as_json(section, fallback)

   Binds :c:func:`wp_conf_get_section`

   Returns the raw JSON value of a configuration section. This is the most
   general accessor and the one to use for rules sections, which are arrays of
   match/update objects.

   :param string section: the name of the section, e.g. ``"monitor.v4l2.rules"``
   :param Json fallback: *(optional)* the value to return if the section does
      not exist
   :returns: the section, the fallback, or nil if neither is available
   :rtype: Json, see :ref:`lua_json_api`

.. function:: Conf.get_section_as_properties(section, defaults)

   Returns a JSON object section as a :ref:`Properties <lua_properties_api>`
   object.

   :param string section: the name of the section
   :param defaults: *(optional)* a table or ``Properties`` object with default
      values; values found in the configuration are applied on top of these
   :returns: the properties of the section
   :rtype: Properties

.. function:: Conf.get_section_as_object(section, defaults)

   Returns a JSON object section as a Lua table.

   :param string section: the name of the section
   :param defaults: *(optional)* a table with default values
   :returns: the contents of the section
   :rtype: table

.. function:: Conf.get_section_as_array(section, defaults)

   Returns a JSON array section as a Lua table.

   :param string section: the name of the section
   :param defaults: *(optional)* a table to return if the section does not
      exist or is not an array
   :returns: the contents of the section
   :rtype: table

Opening a different configuration
---------------------------------

These methods are only needed by code that works with a ``Conf`` object other
than the core's own configuration.

.. function:: Conf.open(self)

   Binds :c:func:`wp_conf_open`

   Opens the configuration file and its fragments.

   :returns: nil on success, or an error message string on failure
   :rtype: string

.. function:: Conf.close(self)

   Binds :c:func:`wp_conf_close`

   Closes the configuration file.
