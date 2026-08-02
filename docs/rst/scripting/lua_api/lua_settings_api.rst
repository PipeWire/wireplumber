.. _lua_settings_api:

Settings
========

The ``Settings`` API gives scripts access to WirePlumber's runtime settings.
Unlike static configuration, settings can be changed while WirePlumber is
running — from ``wpctl settings``, or by any client writing to the
``sm-settings`` metadata — and scripts can subscribe to be notified when they
change.

Every setting must be declared in the ``wireplumber.settings.schema`` section
of the configuration, which defines its type, default value and (for numbers)
its valid range. See :ref:`config_settings` for the list of settings that ship
with WirePlumber and :ref:`config_modifying_configuration` for how to change
them.

This binds the :ref:`WpSettings <settings_api>` C API. All the functions are
static; the underlying ``WpSettings`` object is looked up automatically.

Reading settings
----------------

.. function:: Settings.get(name)

   Binds :c:func:`wp_settings_get`

   Returns the current value of a setting as raw JSON. The typed accessors
   below are usually more convenient.

   :param string name: the name of the setting
   :returns: the value of the setting, or nil if it does not exist
   :rtype: Json, see :ref:`lua_json_api`

.. function:: Settings.get_boolean(name)

   Returns the value of a boolean setting. If the setting does not exist or is
   not a boolean, false is returned.

   :param string name: the name of the setting
   :returns: the value of the setting
   :rtype: boolean

.. function:: Settings.get_int(name)

   Returns the value of an integer setting, or 0 if it does not exist or is not
   an integer.

   :param string name: the name of the setting
   :returns: the value of the setting
   :rtype: integer

.. function:: Settings.get_float(name)

   Returns the value of a float setting, or 0 if it does not exist or is not a
   float.

   :param string name: the name of the setting
   :returns: the value of the setting
   :rtype: number

.. function:: Settings.get_string(name)

   Returns the value of a string setting, or nil if it does not exist or is not
   a string.

   :param string name: the name of the setting
   :returns: the value of the setting
   :rtype: string

.. function:: Settings.get_array(name)

   Returns the value of an array setting as a Lua table. If the setting does
   not exist or is not an array, an empty table is returned.

   :param string name: the name of the setting
   :returns: the value of the setting
   :rtype: table

.. function:: Settings.get_object(name)

   Returns the value of a JSON object setting as a Lua table. If the setting
   does not exist or is not an object, an empty table is returned.

   :param string name: the name of the setting
   :returns: the value of the setting
   :rtype: table

.. function:: Settings.get_saved(name)

   Binds :c:func:`wp_settings_get_saved`

   Returns the value of the setting as it was persisted to the state file, if
   it has been saved. This may differ from the current value.

   :param string name: the name of the setting
   :returns: the saved value of the setting, or nil if it has not been saved
   :rtype: Json, see :ref:`lua_json_api`

.. function:: Settings.iterate()

   Binds :c:func:`wp_settings_new_iterator`

   Iterates over all the settings. Intended to be used in a ``for`` loop; the
   iteration yields the setting name and its value on each step.

   .. code-block:: lua

      for name, value in Settings.iterate () do
        log:info (name .. " = " .. value:to_string ())
      end

Changing settings
-----------------

.. note::

   Scripts do not normally need to change settings; this is what
   ``wpctl settings`` and other clients do. These functions exist for scripts
   that implement such a control interface.

.. function:: Settings.set(name, value)

   Binds :c:func:`wp_settings_set`

   Changes the current value of a setting. The change is not persistent unless
   :func:`Settings.save` is also called.

   :param string name: the name of the setting
   :param Json value: the new value
   :returns: true if the setting was changed, false otherwise
   :rtype: boolean

.. function:: Settings.reset(name)

   Binds :c:func:`wp_settings_reset`

   Resets a setting to the default value declared in the schema.

   :param string name: the name of the setting
   :returns: true on success, false otherwise
   :rtype: boolean

.. function:: Settings.save(name)

   Binds :c:func:`wp_settings_save`

   Persists the current value of a setting, so that it is restored the next
   time WirePlumber starts.

   :param string name: the name of the setting
   :returns: true on success, false otherwise
   :rtype: boolean

.. function:: Settings.delete(name)

   Binds :c:func:`wp_settings_delete`

   Deletes the persisted value of a setting. The current value is not changed.

   :param string name: the name of the setting
   :returns: true on success, false otherwise
   :rtype: boolean

.. function:: Settings.reset_all()

   Binds :c:func:`wp_settings_reset_all`

   Resets all settings to their default values.

.. function:: Settings.save_all()

   Binds :c:func:`wp_settings_save_all`

   Persists the current value of all settings.

.. function:: Settings.delete_all()

   Binds :c:func:`wp_settings_delete_all`

   Deletes the persisted values of all settings.

Reacting to changes
-------------------

.. function:: Settings.subscribe(pattern, callback)

   Binds :c:func:`wp_settings_subscribe_closure`

   Calls *callback* whenever a setting whose name matches *pattern* changes.

   This is the idiomatic way for a script to respond to a setting: read the
   value once at load time, and then again from the callback. Scripts commonly
   use this to register or remove an event hook, so that the hook only exists
   while the setting is enabled — see :ref:`lua_events_api_toggle`.

   .. code-block:: lua

      Settings.subscribe ("linking.allow-moving-streams", function ()
        handleMoveSetting (Settings.get_boolean ("linking.allow-moving-streams"))
      end)

   :param string pattern: a glob-style pattern matching setting names;
      use ``"*"`` to subscribe to all settings
   :param function callback: the function to call when a matching setting
      changes
   :returns: an id that can be passed to :func:`Settings.unsubscribe`
   :rtype: integer

.. function:: Settings.unsubscribe(id)

   Binds :c:func:`wp_settings_unsubscribe`

   Cancels a subscription previously created with :func:`Settings.subscribe`.

   :param integer id: the subscription id
   :returns: true if the subscription was found and removed
   :rtype: boolean
