.. _understanding_wireplumber:

Understanding WirePlumber
=========================

Knowing the fundamentals of session management, let's see here how WirePlumber
is structured.

The Library
-----------

libwireplumber wraps PipeWire API and provides higher level and more convenient
API. It provides fundamental building blocks for expressing all the session
management logic. WirePlumber daemon implements the session
management  logic. However this library can also be used outside the
scope of the WirePlumber daemon in order to build external tools and GUIs that
interact with PipeWire. It is written in C and is GObject based.

The Object Model
^^^^^^^^^^^^^^^^

The most fundamental code contained in the WirePlumber library is the object
model, i.e. its representation of PipeWire's objects.

PipeWire exposes several objects, such nodes and ports, via the IPC protocol
in a manner that is hard to interact with using standard object-oriented
principles, because it is asynchronous. For example, when an object is created,
its existence is announced over the protocol, but its properties are announced
later, on a secondary message. If something needs to react on this object
creation event, it typically needs to access the object's properties, so it
must wait until the properties have been sent. Doing this might sound simple,
and it is, but it becomes a tedious repetitive process to be doing this
everywhere instead of focusing on writing the actual event handling logic.

WirePlumber's library solves this by creating proxy objects that cache all the
information and updates received from PipeWire throughout each object's
lifetime. Then, it makes them available via the `WpObjectManager` API, which has
the ability to wait until certain information (ex, the properties) has been
cached on each object before announcing it.

Session management utilities
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Daemon
----------
WirePlumber Daemon runs on top of the API and does the session management. By
itself, it doesn't do anything except load the components. The actual logic is
implemented inside those components. Modules and Lua scripts are two types of
components.

Modules
^^^^^^^
Modules extent the libwireplumber API for specific purposes, they either expose
their own APIs or implement some specific functionality. They are written is C.


Lua Scripts
^^^^^^^^^^^
Lua Scripts implement most of the session management. The libwireplumber API is
available in Lua and so it is very easy to write session management logic in
Lua,as it is a very light scripting language.

.. note::

    WirePlumber can be extended to support scripting system other than Lua.
