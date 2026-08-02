.. _tools_wpexec:

wpexec(1)
=========

SYNOPSIS
--------

**wpexec** *SCRIPT* [*ARGUMENTS*]

DESCRIPTION
-----------

**wpexec** is the WirePlumber script interpreter. It runs a single Lua script
against a running PipeWire daemon, without starting a full session manager.

This makes it the tool of choice for developing and testing scripts, for
one-off queries and for small standalone policy scripts that run alongside the
main WirePlumber instance.

When invoked, **wpexec** connects to PipeWire, activates the plugins that the
script requires through :func:`Core.require_api`, and then runs the script. It
keeps running until the script calls :func:`Core.quit`, the connection to
PipeWire is lost, or it is interrupted with SIGINT, SIGTERM or SIGHUP.

Scripts run under **wpexec** have access to the same
:ref:`Lua API <scripting_lua_api>` as scripts loaded by the daemon, with the
exception of a few functions that only make sense inside the daemon.

ARGUMENTS
---------

*SCRIPT*
  The path to the Lua script to execute.

*ARGUMENTS*
  An optional SPA-JSON **object** that is passed to the script as its argument
  (``...``). Passing anything that is not a JSON object is an error.

EXAMPLES
--------

Run a script:

.. code-block:: console

   $ wpexec ./get-default-sink-volume.lua

Run a script with arguments:

.. code-block:: console

   $ wpexec ./interactive.lua '{ option1 = "value1", option2 = "value2" }'

Run a script from a build tree, without installing:

.. code-block:: console

   $ ./wp-uninstalled.sh wpexec tests/examples/interactive.lua

A script may also be made directly executable by giving it the appropriate
shebang line, as the examples in ``tests/examples`` do::

   #!/usr/bin/wpexec

ENVIRONMENT
-----------

**wpexec** honours the same environment variables as **wireplumber**\ (1),
most usefully **WIREPLUMBER_DEBUG**; see :ref:`daemon_logging`.

EXIT STATUS
-----------

Exit codes follow the conventions of **sysexits.h**\ (3head):

**0** (*EX_OK*)
  Success.

**64** (*EX_USAGE*)
  Command line usage error, including a malformed *ARGUMENTS* object.

**69** (*EX_UNAVAILABLE*)
  Could not connect to PipeWire.

**70** (*EX_SOFTWARE*)
  Internal software error, including an error raised by the script.

SEE ALSO
--------

**wireplumber**\ (1), **wpctl**\ (1), **pipewire**\ (1), **sysexits.h**\ (3head)

WirePlumber Documentation: https://pipewire.pages.freedesktop.org/wireplumber/
