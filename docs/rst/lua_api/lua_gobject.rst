.. _lua_gobject:

GObject Integration
===================

The Lua engine that powers WirePlumber's scripts provides direct integration
with `GObject`_. Most of the objects that you will deal with in the lua scripts
are wrapping GObjects. In order to work with the scripts, you will first need
to have a basic understanding of GObject's basic concepts, such as signals and
properties.

Properties
----------

All GObjects have the ability to have `properties`_.
In C we normally use `g_object_get`_ to retrieve them and `g_object_set`_
to set them.

In WirePlumber's lua engine, these properties are exposed as object members
of the Lua object.

For example:

.. code-block:: lua

   -- read the "bound-id" GObject property from the proxy
   local proxy = function_that_returns_a_wp_proxy()
   local proxy_id = proxy["bound-id"]
   print("Bound ID: " .. proxy_id)

Writable properties can also be set in a similar fashion:

.. code-block:: lua

   -- set the "scale" GObject property to the enum value "cubic"
   local mixer = ...
   mixer["scale"] = "cubic"

Signals
-------

GObjects also have a generic mechanism to deliver events to external callbacks.
These events are called `signals`_.
To connect to a signal and handle it, you may use the *connect* method:

.. function:: GObject.connect(self, detailed_signal, callback)

   Connects the signal to a callback. When the signal is emitted by the
   underlying object, the callback will be executed.

   The signature of the callback is expected to match the signature of the
   signal, with the first parameter being the object itself.

   **Example:**

   .. code-block:: lua

      -- connects the "bound" signal from WpProxy to a callback
      local proxy = function_that_returns_a_wp_proxy()
      proxy:connect("bound", function(p, id)
        print("Proxy " .. tostring(p) .. " bound to " .. tostring(id))
      end)

   In this example, the ``p`` variable in the callback is the ``proxy`` object,
   while ``id`` is the first parameter of the *"bound"* signal, as documented
   in :c:struct:`WpProxy`

   :param detailed_signal: the signal name to listen to
                           (of the form "signal-name::detail")
   :param callback: a lua function that will be called when the signal is emitted

Signals may also be used as a way to have dynamic methods on objects. These
signals are meant to be called by external code and not handled. These signals
are called **action signals**.
You may call an action signal using the *call* method:

.. function:: GObject.call(self, action_signal, ...)

   Calls an action signal on this object.

   **Example:**

   .. code-block:: lua

      Core.require_api("default-nodes", "mixer", function(...)
        local default_nodes, mixer = ...

        -- "get-default-node" and "get-volume" are action signals of the
        -- "default-nodes-api" and "mixer-api" plugins respectively
        local id = default_nodes:call("get-default-node", "Audio/Sink")
        local volume = mixer:call("get-volume", id)

        -- the return value of "get-volume" is a GVariant(a{sv}),
        -- which gets translated to a Lua table
        Debug.dump_table(volume)
      end)

   :param action_signal: the signal name to call
   :param ...: a list of arguments that will be passed to the signal
   :returns: the return value of the action signal, if any

Type conversions
----------------

When working with GObject properties and signals, variables need to be
converted from C types to Lua types and vice versa. The following tables
list the type conversions that happen automatically:

C to Lua
^^^^^^^^

Conversion from C to lua is based on the C type.

================================ ===============================================
              C                                        Lua
================================ ===============================================
gchar, guchar, gint, guint       integer
glong, gulong, gint64, guint64   integer
gfloat, gdouble                  number
gboolean                         boolean
gchar *                          string
gpointer                         lightuserdata
WpProperties *                   table (keys: string, values: string)
enum                             string containing the nickname (short name) of
                                 the enum, or integer if the enum is not
                                 registered with GType
flags                            integer (as in C)
GVariant *                       a native type, see below
other GObject, GInterface        userdata holding reference to the object
other GBoxed                     userdata holding reference to the object
================================ ===============================================

Lua to C
^^^^^^^^

Conversion from Lua to C is based on the expected type in C.

============================== ==================================================
           Expecting                                  Lua
============================== ==================================================
gchar, guchar, gint, guint,    convertible to integer
glong, gulong, gint64, guint64 convertible to integer
gfloat, gdouble                convertible to number
gboolean                       convertible to boolean
gchar *                        convertible to string
gpointer                       must be lightuserdata
WpProperties *                 must be table (keys: string, values: convertible
                               to string)
enum                           must be string holding the nickname of the enum,
                               or convertible to integer
flags                          convertible to integer
GVariant *                     see below
other GObject, GInterface      must be userdata holding a compatible GObject type
other GBoxed                   must be userdata holding the same GBoxed type
============================== ==================================================

GVariant to Lua
^^^^^^^^^^^^^^^

============================= =============================================
          GVariant                                 Lua
============================= =============================================
NULL or G_VARIANT_TYPE_UNIT   nil
G_VARIANT_TYPE_INT16          integer
G_VARIANT_TYPE_INT32          integer
G_VARIANT_TYPE_INT64          integer
G_VARIANT_TYPE_UINT16         integer
G_VARIANT_TYPE_UINT32         integer
G_VARIANT_TYPE_UINT64         integer
G_VARIANT_TYPE_DOUBLE         number
G_VARIANT_TYPE_BOOLEAN        boolean
G_VARIANT_TYPE_STRING         string
G_VARIANT_TYPE_VARIANT        converted recursively
G_VARIANT_TYPE_DICTIONARY     table (keys & values converted recursively)
G_VARIANT_TYPE_ARRAY          table (children converted recursively)
============================= =============================================

Lua to GVariant
^^^^^^^^^^^^^^^

Conversion from Lua to GVariant is based on the lua type and is quite limited.

There is no way to recover an array, for instance, because there is no way
in Lua to tell if a table contains an array or a dictionary. All Lua tables
are converted to dictionaries and integer keys are converted to strings.

========= ================================
   Lua                GVariant
========= ================================
nil       G_VARIANT_TYPE_UNIT
boolean   G_VARIANT_TYPE_BOOLEAN
integer   G_VARIANT_TYPE_INT64
number    G_VARIANT_TYPE_DOUBLE
string    G_VARIANT_TYPE_STRING
table     G_VARIANT_TYPE_VARDICT (a{sv})
========= ================================

Closures
--------

When a C function is expecting a GClosure, in Lua it is possible to pass
a Lua function directly. The function is then wrapped into a custom GClosure.

When this GClosure is invalidated, the reference to the Lua function is dropped.
Similarly, when the lua engine is stopped, all the GClosures that were
created by this engine are invalidated.

Reference counting
------------------

GObject references in Lua always hold a reference to the underlying GObject.
When moving this reference around to other variables in Lua, the underlying
GObject reference is shared, but Lua reference counts the wrapper "userdata"
object.

.. code-block:: lua

   -- creating a new FooObject instance; obj holds the GObject reference
   local obj = FooObject()

   -- GObject reference is dropped and FooObject is finalized
   obj = nil

.. code-block:: lua

   -- creating a new FooObject instance; obj holds the GObject reference
   local obj = FooObject()

   function store_global(o)
     -- o is now stored in the global 'obj_global' variable
     -- the GObject ref count is still 1
     obj_global = o
   end

   -- obj userdata reference is passed to o, the GObject ref count is still 1
   store_global(obj)

   -- userdata reference dropped from obj, the GObject is still alive
   obj = nil

   -- userdata reference dropped from obj_global,
   -- the GObject ref is dropped and FooObject is finalized
   obj_global = nil

.. note::

   When assigning a variable to nil, Lua may not immediately drop
   the reference of the underlying object. This is because Lua uses a garbage
   collector and goes through all the unreferenced objects to cleanup when
   the garbage collector runs.

When a GObject that is already referenced in Lua re-appears somewhere else
through calling some API or because of a callback from C, a new reference is
added on the GObject.

.. code-block:: lua

   -- ObjectManager is created in Lua, om holds 1 ref
   local om = ObjectManager(...)
   om:connect("objects-changed", function (om)
     -- om in this scope is a local function argument that was created
     -- by the signal's closure marshaller and holds a second reference
     -- to the ObjectManager

     do_some_stuff()

     -- this second reference is dropped when the function goes out of scope
   end)

.. danger::

   Because Lua variables hold strong references to GObjects, it is dangerous
   to create closures that reference such variables, because these closures
   may create reference loops and **leak** objects

.. code-block:: lua

   local om = ObjectManager(...)

   om:connect("objects-changed", function (obj_mgr)
     -- using 'om' here instead of the local 'obj_mgr'
     -- creates a dangerous reference from the closure to 'om'
     for obj in om:iterate() do
        do_stuff(obj)
     end
   end)

   -- local userdata reference dropped, but the GClosure that was generated
   -- from the above function is still holding a reference and keeps
   -- the ObjectManager alive; the GClosure is referenced by the ObjectManager
   -- because of the signal connection, so the ObjectManager is leaked
   om = nil

.. _GObject: https://developer.gnome.org/gobject/stable/
.. _properties: https://developer.gnome.org/gobject/stable/gobject-properties.html
.. _g_object_get: https://developer.gnome.org/gobject/stable/gobject-The-Base-Object-Type.html#g-object-get
.. _g_object_set: https://developer.gnome.org/gobject/stable/gobject-The-Base-Object-Type.html#g-object-set
.. _signals: https://developer.gnome.org/gobject/stable/signal.html
