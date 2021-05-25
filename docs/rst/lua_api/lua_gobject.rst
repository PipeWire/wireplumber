.. _lua_gobject:

GObject Integration
===================

The Lua engine that powers WirePlumber's scripts provides direct integration
with `GObject`_. Most of the objects that you will deal with in the lua scripts
are wrapping GObjects. In order to work with the scripts, you will first need
to have a basic understanding of GObject's basic concepts, such as signals and
properties.

Properties
..........

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
.......

GObjects also have a generic mechanism to deliver events to external callbacks.
These events are called `signals`_

All lua objects that wrap a GObject contain the following methods:

.. function:: connect(detailed_signal, callback)

   Connects the signal to a callback. When the signal is emitted by the
   underlying object, the callback will be executed.

   :param detailed_signal: the signal name to listen to
                           (of the form "signal-name::detail")
   :param callback: a lua function that will be called when the signal is emitted

.. function:: call(action_signal, ...)

   Calls an action signal on this object.

   :param action_signal: the signal name to call
   :param ...: a list of arguments that will be passed to the signal

.. _GObject: https://developer.gnome.org/gobject/stable/
.. _properties: https://developer.gnome.org/gobject/stable/gobject-properties.html
.. _g_object_get: https://developer.gnome.org/gobject/stable/gobject-The-Base-Object-Type.html#g-object-get
.. _g_object_set: https://developer.gnome.org/gobject/stable/gobject-The-Base-Object-Type.html#g-object-set
.. _signals: https://developer.gnome.org/gobject/stable/signal.html
