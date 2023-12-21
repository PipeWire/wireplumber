.. _understanding_wireplumber:

Understanding WirePlumber
=========================

Knowing the fundamentals of session management, let's see here how WirePlumber
is structured.

The library
-----------

WirePlumber is built on top of the `libwireplumber` library, which provides
fundamental building blocks for expressing all the session management logic.
Libwireplumber, which is written in C and based on GObject, wraps the PipeWire
API and offers a higher level and more convenient API. While the WirePlumber
daemon implements the session management logic, the underlying library can also
be utilized outside the scope of the WirePlumber daemon. This allows for the
creation of external tools and GUIs that interact with PipeWire.

Being GObject based, the library is introspectable and can be used from any
language that supports `GObject Introspection <https://gi.readthedocs.io/en/latest/>`_.
The library is also available as a C API.

The object model
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
information and updates received from PipeWire throughout each object's lifetime.
Then, it makes them available via the :ref:`WpObjectManager <obj_manager_api>`
API, which has the ability to wait until certain information (ex, the
properties) has been cached on each object before announcing it.

Session management utilities
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The library also provides a set of utilities that are useful for session
management. For example, it provides the :ref:`WpSessionItem <session_item_api>`
class that can be used to abstract a part of the graph with some logic attached
to it. It also provides the :ref:`Events & Hooks API <events_and_hooks>`, which
is a way to express event handling logic in a declarative way.

Misc utilities
^^^^^^^^^^^^^^

The library also provides a set of miscellaneous utilities that bridge the
PipeWire API with the GObject API, such as :ref:`WpProperties <properties_api>`,
:ref:`WpSpaPod <spa_pod_api>`, :ref:`WpSpaJson <spa_json_api>` and others.
These complement the object model and make it easier to interact with PipeWire
objects.

The daemon
----------

The WirePlumber daemon is implemented on top of the library API and its job is
to host components that implement the session management logic. By itself, it
doesn't do anything except load and activate these components. The actual logic
is implemented inside them.

Modular design ensures that it is possible to swap the implementation of
specific functionality without having to re-implement the rest of it, allowing
flexibility on target-sensitive parts, such as policy management and
making use of non-standard hardware.

There are several kinds of components, each with a different purpose. The two
main kinds are `modules` and `Lua scripts`. See also
:ref:`config_components_and_profiles`.

Modules
^^^^^^^

Modules extend the libwireplumber API for specific purposes, usually to provide
support code or to allow WirePlumber to communicate with external services (ex
D-Bus APIs). They either expose their own APIs via GObject signals (i.e. dynamic
APIs that do not require linking at compile time) or implement certain objects
through interfaces. They are written in C.

Lua scripts
^^^^^^^^^^^

Lua scripts implement most of the session management. The libwireplumber API is
made available in Lua with idiomatic bindings that make it very easy to write
session management logic.

Lua has been chosen because it is a very lightweight scripting language that is
suitable for embedding. It is also very easy to learn and use, as well as bind
it to C code. However, WirePlumber can be easily extended to support scripting
languages other than Lua. The entire Lua scripting system is implemented as a
module.
