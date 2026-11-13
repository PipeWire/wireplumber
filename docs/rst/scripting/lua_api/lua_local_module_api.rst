.. _lua_local_module_api:

Local Modules
=============

The `LocalModule` object (which binds the :c:struct:`WpImplModule` C API) provides a way
to load PipeWire modules in the WirePlumber process. Instantiating the object
does not load the module; it is loaded by activating
``Feature.LocalModule.LOADED`` and unloaded by deactivating that feature, or
when the last reference to the object is dropped.

The module is loaded in the *client context*: a secondary ``pw_context`` that
runs on its own thread, so that the module is never held up by whatever
WirePlumber's main loop is doing. See ``support.client-context`` in
:ref:`config_features`. If that feature is disabled, the module is loaded in
WirePlumber's main ``pw_context`` instead.

Constructors
~~~~~~~~~~~~

.. function:: LocalModule(name, arguments, properties)

   Creates a new module object with the provided arguments and properties
   (either of which can be ``nil``), without loading it.

   :param string name: the module name, such as ``"libpipewire-module-loopback"``
   :param string arguments: should be either ``nil`` or a string with the desired
        module arguments
   :param table properties: can be ``nil`` or a table that can be
        :ref:`converted <lua_gobject_lua_to_c>` to :c:struct:`WpProperties`
   :returns: a new LocalModule
   :rtype: LocalModule (:c:struct:`WpImplModule`)
   :since: 0.4.2

Methods
~~~~~~~

LocalModule has no methods of its own; it is loaded and unloaded with the
:ref:`object <lua_object_api>` ``activate`` and ``deactivate`` methods.

Example:

.. code-block:: lua

   local module = LocalModule("libpipewire-module-loopback", args, {})
   module:activate(Feature.LocalModule.LOADED, function (m, e)
     if e then
       Log.warning("failed to load the module: " .. tostring(e))
     end
   end)
   -- ... later, when the module is no longer needed:
   module:deactivate(Feature.LocalModule.LOADED)
