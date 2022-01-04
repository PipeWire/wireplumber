.. _lua_introduction:

Introduction
============

`Lua <https://www.lua.org/>`_ is a powerful, efficient, lightweight,
embeddable scripting language.

WirePlumber uses `Lua version 5.3 <https://www.lua.org/versions.html>`_ to
implement its engine. Another, more recent, version may be considered
in the future, but do note that different Lua versions are not API-compatible
and that will likely also affect WirePlumber's Lua API.

There are currently two uses for Lua in WirePlumber:

  - To implement the scripting engine
  - To implement lua-based :ref:`config files <config_lua>`

This section is only documenting the API of the **scripting engine**

Lua Reference
-------------

If you are not familiar with the Lua language and its API, please refer to
the `Lua 5.3 Reference Manual <https://www.lua.org/manual/5.3/manual.html>`_

Sandbox
-------

WirePlumber's scripting engine sandboxes the lua scripts to a safe environment.
In this environment, the following rules apply:

  - Scripts are isolated from one another; global variables in one script
    are not visible from another, even though they are actually executed in
    the same ``lua_State``

  - Tables that hold API methods are not writable. While this may sound strange,
    standard Lua allows you to change standard API, for instance
    ``string.format = rogue_format`` is valid outside the sandbox.
    WirePlumber does not allow that.

  - The standard Lua API is limited only to safe functions. Functions that
    interact with the file system, the lua modules system, the lua state,
    the process's state, etc are **not** allowed.

    Here is a full list of Lua functions (and API tables) that are exposed:

    .. literalinclude:: ../../../lib/wplua/sandbox.lua
      :language: lua
      :lines: 40-55

  - Object methods are not exposed in public tables. To call an object method
    you must use the method call syntax of Lua, i.e. ``object:method(params)``

    The following, for instance, is **not** valid:

    .. code-block:: lua

       -- this will cause an exception
       local node = ...
       Node.send_command(node, "Suspend")

    The correct form is this:

    .. code-block:: lua

       local node = ...
       node:send_command("Suspend")
