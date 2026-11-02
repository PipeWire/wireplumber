.. _lua_core_api:

Core
====

The :ref:`WpCore <core_api>` API is mostly transparent to lua, as the core
object is not exposed to the scripts.

For some functionality, though, the following static functions are exposed.

.. function:: Core.get_properties()

   Binds :c:func:`wp_connection_get_properties`

   Returns a table with the properties of the core. These are the same
   properties that appear on WirePlumber's *client* object in the PipeWire
   global registry.

   :returns: the properties of the core
   :rtype: table

.. function:: Core.get_info()

   Returns a table with information about the core. The table contains
   the following fields:

   =========== ===========
   Field       Contains
   =========== ===========
   cookie      The value of :c:func:`wp_connection_get_remote_cookie`
   name        The value of :c:func:`wp_connection_get_remote_name`
   user_name   The value of :c:func:`wp_connection_get_remote_user_name`
   host_name   The value of :c:func:`wp_connection_get_remote_host_name`
   version     The value of :c:func:`wp_connection_get_remote_version`
   properties  The value of :c:func:`wp_connection_get_remote_properties`
   =========== ===========

   :returns: information about the core
   :rtype: table

.. function:: Core.update_properties(properties)

   Binds :c:func:`wp_connection_update_properties`

   Updates the properties of the core, which are the properties that appear on
   WirePlumber's *client* object in the PipeWire global registry.

   :param properties: a table or :ref:`Properties <lua_properties_api>` object
      with the properties to set

.. function:: Core.get_own_bound_id()

   Binds :c:func:`wp_connection_get_own_bound_id`

   :returns: the global id of WirePlumber's own client object
   :rtype: integer

.. function:: Core.get_vm_type()

   Binds :c:func:`wp_context_get_vm_type`

   Detects whether WirePlumber is running inside a virtual machine. The ALSA
   monitor uses this to apply VM-specific workarounds.

   :returns: the name of the virtual machine type, or nil if not running in a
      virtual machine
   :rtype: string

.. function:: Core.idle_add(callback)

   Binds :c:func:`wp_context_idle_add_closure`

   Schedules to call *callback* the next time that the event loop will be idle

   :param function callback: the function to call; the function takes no
      arguments and must return true/false, as it is a GSourceFunc:
      return true to have the event loop call it again the next time it is idle,
      false to stop calling it and remove the associated GSource
   :returns: the GSource associated with this idle callback
   :rtype: GSource, see :func:`GSource.destroy`

.. function:: Core.timeout_add(timeout_ms, callback)

   Binds :c:func:`wp_context_timeout_add_closure`

   Schedules to call *callback* after *timeout_ms* milliseconds

   :param function callback: the function to call; the function takes no
      arguments and must return true/false, as it is a GSourceFunc:
      return true to have the event loop call it again periodically every
      *timeout_ms* milliseconds, false to stop calling it and remove the
      associated GSource
   :returns: the GSource associated with this idle callback
   :rtype: GSource, see :func:`GSource.destroy`

.. function:: GSource.destroy(self)

   For the purpose of working with :func:`Core.idle_add` and
   :func:`Core.timeout_add`, the GSource object that is returned by those
   functions contains this method.

   Call this method to destroy the source, so that the associated callback
   is not called again. This can be used to stop a repeating timer callback
   or just to abort some idle operation

   This method binds *g_source_destroy*

.. function:: Core.sync(callback)

   Binds :c:func:`wp_connection_sync`

   Calls *callback* after synchronizing the transaction state with PipeWire

   :param function callback: a function to be called after syncing with PipeWire;
      the function takes an error string, if something went wrong and nil
      otherwise, and the sequence number of the sync operation, or nil if
      something went wrong; it returns nothing
   :returns: the sequence number of the sync operation, or a negative errno if
      an error occurred before returning from this function
   :rtype: integer

.. function:: Core.quit()

   Quits the current *wpexec* process

   .. note::

      This can only be called when the script is running in *wpexec*;
      if it is running in the main WirePlumber daemon, it will print
      a warning and do nothing

.. function:: Core.require_api(..., callback)

   Ensures that the specified API plugins are loaded.

   API plugins are plugins that provide some API extensions for use in scripts.
   These plugins must always have their name end in "-api" and the names
   specified here must not have the "-api" extension.

   For instance, the "mixer-api" module provides an API to change volume/mute
   controls from scripts, via action signals. It can be used like this:

   .. code-block:: lua

      Core.require_api("mixer", function(mixer)
        -- get the volume of an audio stream
        local volume = mixer:call("get-volume", stream)

        -- the return value of "get-volume" is a GVariant(a{sv}),
        -- which gets translated to a Lua table
        Debug.dump_table(volume)
      end)

   See also the example in :func:`GObject.call`

   .. note::

      This can only be called when the script is running in *wpexec*;
      if it is running in the main WirePlumber daemon, it will print
      a warning and do nothing

   :param strings ...: a list of string arguments, which specify the names of
      the api plugins to load, if they are not already loaded
   :param callback: the function to call after the plugins have been loaded;
      this function takes references to the plugins as parameters

.. function:: Core.test_feature()

   Binds :c:func:`wp_core_test_feature`

   Tests if the specified feature is provided in the current WirePlumber
   configuration.

   :param string feature: the name of the feature to test
   :returns: true if the feature is provided, false otherwise
   :rtype: boolean

.. function:: Core.get_main_context()

   Binds :c:func:`wp_core_get_main_context`

   :returns: the main context of the core
   :rtype: :ref:`MainContext <lua_main_context_api>`

.. function:: Core.get_connection()

   Binds :c:func:`wp_core_get_connection`

   :returns: the connection of the main context of the core
   :rtype: :ref:`Connection <lua_connection_api>`

.. _lua_context_api:

Context
-------

The :ref:`WpContext <main_context_api>` methods are available on the object
returned by :func:`Core.get_main_context`:

.. function:: Context.get_core(self)

   Binds :c:func:`wp_context_get_core`

   :returns: the core of the context, or nil if that core is gone

.. function:: Context.get_spa_libs(self)

   Binds :c:func:`wp_context_get_spa_libs`

   :returns: the "context.spa-libs" description of the context, or nil
   :rtype: WpSpaJson

.. function:: Context.get_modules(self)

   Binds :c:func:`wp_context_get_modules`

   :returns: the "context.modules" description of the context, or nil
   :rtype: WpSpaJson

.. function:: Context.get_properties(self)

   Binds :c:func:`wp_context_get_properties`

   :returns: the properties of the context
   :rtype: :ref:`Properties <lua_properties_api>`

.. function:: Context.get_vm_type(self)

   Binds :c:func:`wp_context_get_vm_type`

   :returns: a comma separated string with the virtual machine types that this
      context matches, or nil if not running in a virtual machine
   :rtype: string

.. function:: Context.idle_add(self, callback)

   Binds :c:func:`wp_context_idle_add_closure`

   The same as :func:`Core.idle_add`, using this context.

   :param function callback: the function to call
   :returns: the GSource associated with this idle callback
   :rtype: GSource, see :func:`GSource.destroy`

.. function:: Context.timeout_add(self, timeout_ms, callback)

   Binds :c:func:`wp_context_timeout_add_closure`

   The same as :func:`Core.timeout_add`, using this context.

   :param integer timeout_ms: the interval, in milliseconds
   :param function callback: the function to call
   :returns: the GSource associated with this timeout callback
   :rtype: GSource, see :func:`GSource.destroy`

.. _lua_main_context_api:

MainContext
-----------

.. function:: MainContext.get_connection(self)

   Binds :c:func:`wp_main_context_get_connection`

   :returns: the connection of the context to PipeWire
   :rtype: :ref:`Connection <lua_connection_api>`

.. _lua_connection_api:

Connection
----------

The :ref:`WpConnection <connection_api>` object is returned by
:func:`MainContext.get_connection` and :func:`Core.get_connection`.

.. function:: Connection.get_core(self)

   Binds :c:func:`wp_connection_get_core`

   :returns: the core that uses the connection, or nil if that core is gone

.. function:: Connection.get_own_bound_id(self)

   Binds :c:func:`wp_connection_get_own_bound_id`

   :returns: the global id of the client object of this connection
   :rtype: integer

.. function:: Connection.get_remote_cookie(self)

   Binds :c:func:`wp_connection_get_remote_cookie`

   :returns: the cookie of the PipeWire instance
   :rtype: integer

.. function:: Connection.get_remote_name(self)

   Binds :c:func:`wp_connection_get_remote_name`

   :returns: the name of the PipeWire instance
   :rtype: string

.. function:: Connection.get_remote_user_name(self)

   Binds :c:func:`wp_connection_get_remote_user_name`

   :returns: the name of the user that started the PipeWire instance
   :rtype: string

.. function:: Connection.get_remote_host_name(self)

   Binds :c:func:`wp_connection_get_remote_host_name`

   :returns: the name of the host where the PipeWire instance runs
   :rtype: string

.. function:: Connection.get_remote_version(self)

   Binds :c:func:`wp_connection_get_remote_version`

   :returns: the version of the PipeWire instance
   :rtype: string

.. function:: Connection.get_remote_properties(self)

   Binds :c:func:`wp_connection_get_remote_properties`

   :returns: the properties of the PipeWire instance
   :rtype: :ref:`Properties <lua_properties_api>`

.. function:: Connection.get_properties(self)

   Binds :c:func:`wp_connection_get_properties`

   :returns: the properties of the connection
   :rtype: :ref:`Properties <lua_properties_api>`

.. function:: Connection.update_properties(self, properties)

   Binds :c:func:`wp_connection_update_properties`

   :param properties: a table or :ref:`Properties <lua_properties_api>` object
      with the properties to set

.. function:: Connection.sync(self, callback)

   Binds :c:func:`wp_connection_sync`

   Calls *callback* after synchronizing the transaction state with PipeWire

   :param function callback: the same as in :func:`Core.sync`
   :returns: the sequence number of the sync operation, or a negative errno if
      an error occurred before returning from this function
   :rtype: integer
