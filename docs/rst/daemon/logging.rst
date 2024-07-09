.. _daemon_logging:

Debug Logging
=============

WirePlumber is instrumented with log messages in its entire codebase. These
messages are categorized based on two heuristics: the log topic and the log
level.

The log topic is a string that identifies which component of the code this
message is coming from. Well-known topics include:

  - **wireplumber**: messages from the wireplumber daemon
  - **wp-***: messages from libwireplumber

   - **wp-core**: messages from *WpCore*
   - **wp-proxy**: messages from *WpProxy*
   - ... and so on ...

  - **m-***: messages from wireplumber modules

   - **m-default-profile**: messages from *libwireplumber-module-default-profile*
   - **m-default-routes**: messages from *libwireplumber-module-default-routes*
   - ... and so on ...

  - **s-***: messages from scripts

   - **s-linking**: messages from the *linking/\*.lua* scripts
   - **s-default-nodes**: messages from the *default-nodes/\*.lua* scripts
   - ... and so on ...

  - **pw.***: messages from libpipewire
  - **spa.***: messages from spa plugins
  - **mod.***: messages from libpipewire modules
  - **conn.***: messages to debug the pipewire socket connection

The log level is a value that designates the importance of the message.
The levels that exist in WirePlumber are the following:

  - ``F``: *Fatal errors*. These messages represent situations where execution
    of the program cannot continue. In the extremely unlikely case that
    they appear, these messages also cause the process to be terminated.
  - ``E``: *Critical warnings* (or "errors" in the PipeWire terminology).
    These messages represent situations where something unexpected has happened
    and someone with understanding of the code should probably take a look at it.
    These situations are usually programming mistakes or omissions.
    This does not necessarily mean that the program is not functioning correctly.
    It may mean, though, that the specific part of the program that logged the
    message (the plugin, subsystem, ...) may not work optimally.
  - ``W``: *Warnings*. These messages represent situations where something has
    gone unintentionally wrong, but it was not totally unexpected. The situation
    is recovered and the program can continue. In many cases, this warning may
    mean that there is something wrong with the configuration or the environment
    and may need attention from the user.
  - ``N``: *Notices*. These are important messages that the user should notice,
    like warnings, but they do not necessarily mean a bad situation.
  - ``I``: *Informational messages*. These messages provide information about
    the internal operations of the program.
  - ``D``: *Debug messages*. These messages provide details about the
    internal operations of the program, which can be useful for debugging.
  - ``T``: *Traces*. These messages provide very verbose printouts of internal
    operations and data that affects these operations. These can be useful for
    debugging as well, but it may be best to be enabled only for the topic(s)
    that are intended to be debugged, as they can be very big in volume.

By default, WirePlumber logs only messages from levels ``F``, ``E``, ``W``
and ``N``. These messages are printed on the standard error (``stderr``) stream,
or they are logged to the systemd journal, if WirePlumber was started as a
systemd service.

The ``WIREPLUMBER_DEBUG`` environment variable can be used to change which
topics and levels are enabled. The generic syntax is:

.. code::

   WIREPLUMBER_DEBUG=[<topic pattern>:]<level>,...,

This is a comma-separated list of topics to enable, paired with a level for
each topic.

``<level>`` can be one of ``FEWNIDT`` or a numerical log level as listed below.

  0. fatal errors (``F``)
  1. critical warnings (``E``)
  2. warnings and notices (``W`` & ``N``)
  3. informational messages (``I``)
  4. debug messages (``D``)
  5. trace messages (``T``)

Each level always includes messages from the previous levels, so for instance
enabling level ``3`` (or ``I``) will also enable messages from levels ``2``
and ``1`` (``N``, ``W``, ``E`` and ``F``)

``<topic pattern>`` is an *optional* description of one or more topics.
This supports
`glob style patterns <https://developer-old.gnome.org/glib/stable/glib-Glob-style-pattern-matching.html>`_
containing ``*`` and ``?``.

If a ``<topic pattern>`` is not specified, then the given ``<level>`` is
considered to be the global log level, which applies to all topics that have
no explicit level specified.

Changing log level at runtime
-----------------------------

The debug log level can be changed at runtime using ``wpctl``:

.. code::

   wpctl set-log-level D     # enable debug logging for Wireplumber
   wpctl set-log-level -     # restore default logging for Wireplumber

   wpctl set-log-level 0 4   # enable debug logging for Pipewire daemon
   wpctl set-log-level 0 -   # restore default logging for Pipewire daemon

Equivalently, it is also possible to adjust the logging by setting
``log.level`` in the ``settings`` metadata:

.. code::

   pw-metadata -n settings <ID> log.level "D"   # WirePlumber logging

   pw-metadata -n settings 0 log.level 4        # PipeWire daemon logging

Above, ``<ID>`` should be replaced by the WirePlumber daemon client ID.

Note that PipeWire daemon log levels must be specified by numbers, not
letter codes.

Changing log level via static configuration
-------------------------------------------

If you need to capture logs from WirePlumber at startup or in other circumstances
where changing the level at runtime or setting an environment variable is not
feasible, then you may also set the log level in the configuration file.

The log level changes via the ``log.level`` key in the ``context.properties``
section:

.. code::

   context.properties = {
     log.level = "D"
   }

You may use the same syntax as in ``WIREPLUMBER_DEBUG`` to describe the exact
logging you want to achieve. For instance, to log debug messages from all
scripts and informational messages from everywhere else:

.. code::

   context.properties = {
     log.level = "I,s-*:D"
   }

The easiest way to configure this is to drop a
:ref:`fragment file <config_conf_file_fragments>` that contains just this.

.. code-block:: bash

   $ mkdir -p ~/.config/wireplumber/wireplumber.conf.d
   $ echo 'context.properties = { log.level = "D" }' > ~/.config/wireplumber/wireplumber.conf.d/log.conf

See also :ref:`config_modifying_configuration`

Examples
--------

Show *all* messages:

.. code::

   WIREPLUMBER_DEBUG=T

Show all messages up to the *debug* level (F, E, W, N, I & D), excluding *trace*:

.. code::

   WIREPLUMBER_DEBUG=D

Show all messages up to the *notice* level (F, E, W & N),
excluding *info*, *debug* & *trace*
(this is also the default when ``WIREPLUMBER_DEBUG`` is omitted):

.. code::

   WIREPLUMBER_DEBUG=2

Show all messages from the wireplumber library (including traces), but only
up to informational messages from other topics:

.. code::

   WIREPLUMBER_DEBUG=I,wp-*:T

Show debug messages from ``wp-registry``, libpipewire and all modules, keeping
all other topics up to the *notice* level.

.. code::

   WIREPLUMBER_DEBUG=2,wp-registry:4,pw.*:4,m-*:4

Relationship with the GLib log handler & G_MESSAGES_DEBUG
---------------------------------------------------------

Older versions of WirePlumber used to use ``G_MESSAGES_DEBUG`` to control their
log output, which is the environment variable that affects GLib's default
log handler.

As of WirePlumber 0.3, ``G_MESSAGES_DEBUG`` is no longer used, since
libwireplumber replaces the default log handler.

If you are writing your own application based on libwireplumber, you can choose
if you want to replace this log handler using the flags passed to
:c:func:`wp_init`.

Relationship with the PipeWire log handler & PIPEWIRE_DEBUG
-----------------------------------------------------------

libpipewire uses the ``PIPEWIRE_DEBUG`` environment variable, with a similar syntax.
WirePlumber replaces the log handler of libpipewire with its own, rendering
``PIPEWIRE_DEBUG`` useless. Instead, you should use ``WIREPLUMBER_DEBUG``.
All the log topics that apply to libpipewire and its modules / plugins work
the same in ``WIREPLUMBER_DEBUG``.

If you are writing your own application based on libwireplumber, you can choose
if you want to replace this log handler using the flags passed to
:c:func:`wp_init`.

Mapping of PipeWire debug levels to WirePlumber
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

PipeWire supports 5 levels of debug logging. WirePlumber, on the other hand,
supports 7 levels. Some levels seem common, but the terminology and the
semantics are slightly different. The following table shows how the various
levels are mapped:

=============  ===============  ========================
Numeric Level  PipeWire         WirePlumber
=============  ===============  ========================
0              no log           ``F`` - Fatal Error
1              ``E`` - Error    ``E`` - Critical Warning
2              ``W`` - Warning  ``W`` - Warning,
                                ``N`` - Notice
3              ``I`` - Info     ``I`` - Info
4              ``D`` - Debug    ``D`` - Debug
5              ``T`` - Trace    ``T`` - Trace
=============  ===============  ========================
