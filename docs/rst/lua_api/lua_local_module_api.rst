 .. _lua_local_module_api:

Local Modules
=============

The `LocalModule` object (which binds the :c:struct:`WpImplModule` C API) provides a way
to load PipeWire modules in the WirePlumber process. Instantiating the object
loads the module, and when the last reference to the returned module object is
dropped, the module is unloaded.

Constructors
~~~~~~~~~~~~

.. function:: LocalModule(name, arguments, properties)

   Loads the named module with the provided arguments and properties (either of
   which can be nil).

   `name` is the module name, such as `"libpipewire-module-loopback"`.

   `arguments` should be either `nil` or a string with the desired module
   arguments.

   `properties` can be nil or a table that can be :ref:`converted
   <lua_gobject_lua_to_c>` to :c:struct:`WpProperties`.
