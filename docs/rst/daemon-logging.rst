 .. _logging:

Debug Logging
=============

Getting debug messages on the command line is a matter of setting the
``WIREPLUMBER_DEBUG`` environment variable. The generic syntax is:

.. code::

   WIREPLUMBER_DEBUG=level:category1,category2,...

``level`` can be a number from 1 to 5 and defines the minimum debug level to show:

  0. critical warnings and fatal errors (``C`` & ``E`` in the log)
  1. warnings (``W``)
  2. normal messages (``M``)
  3. informational messages (``I``)
  4. debug messages (``D``)
  5. trace messages (``T``)

``category1,category2,...`` is an *optional* comma-separated list of debug
categories to show. Any categories not listed here will *not* appear in the log.
If no categories are specified, then all messages are printed.

Categories support
`glob style patterns <https://developer.gnome.org/glib/stable/glib-Glob-style-pattern-matching.html>`_
containing ``*`` and ``?``, for convenience.

Well known categories include:

  - **wireplumber**: messages from the wireplumber daemon
  - **pw**: messages from libpipewire & spa plugins
  - **wp-***: messages from libwireplumber
  - **wp-core**: messages from *WpCore*
  - **wp-proxy**: messages from *WpProxy*
  - ... and so on ...
  - **m-***: messages from wireplumber modules
  - **m-default-profile**: messages from *libwireplumber-module-default-profile*
  - **m-default-routes**: messages from *libwireplumber-module-default-routes*
  - ... and so on ...
  - **script/***: messages from scripts
  - **script/policy-node**: messages from the *policy-node.lua* script
  - ... and so on ...

Examples
--------

Show all messages:

.. code::

   WIREPLUMBER_DEBUG=5

Show all messages up to the *debug* level (E, C, W, M, I & D), excluding *trace*:

.. code::

   WIREPLUMBER_DEBUG=4

Show all messages up to the *message* level (E, C, W & M),
excluding *info*, *debug* & *trace*
(this is also the default when ``WIREPLUMBER_DEBUG`` is omitted):

.. code::

   WIREPLUMBER_DEBUG=2

Show all messages from the wireplumber library:

.. code::

   WIREPLUMBER_DEBUG=5:wp-*

Show all messages from ``wp-registry``, libpipewire and all modules:

.. code::

   WIREPLUMBER_DEBUG=5:wp-registry,pw,m-*

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
``PIPEWIRE_DEBUG`` useless. Instead, you should use ``WIREPLUMBER_DEBUG`` and the
``pw`` category to control log messages from libpipewire & its plugins.

If you are writing your own application based on libwireplumber, you can choose
if you want to replace this log handler using the flags passed to
:c:func:`wp_init`.

Mapping of PipeWire debug levels to WirePlumber
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Both WirePlumber and PipeWire support 6 levels of debug logging, from 0 to 5

PipeWire uses a slightly different semantic for the first 3 levels:

=====  ========  ===========
Level  PipeWire  WirePlumber
=====  ========  ===========
0      no log    Critical / fatal Errors
1      Errors    Warnings
2      Warnings  Messages
=====  ========  ===========

When PipeWire log messages are printed by the WirePlumber log handler, the
level number stays the same and the semantic changes. PipeWire's errors are
printed in the ``W`` category and PipeWire's warnings are printed in the
``M`` category.

In WirePlumber's (actually GLib's) semantics, this feels more appropriate
because:

  - GLib's errors are fatal (``abort()`` is called)
  - GLib's critical warnings are assertion failures (i.e. programming mistakes,
    not runtime errors)
  - PipeWire's errors are neither fatal, nor programming mistakes; they are
    just bad situations that are not meant to happen
  - GLib's warnings are exactly that: bad runtime situations that are not meant
    to happen, so mapping PipeWire errors to GLib warnings makes sense
  - The **Messages** log level does not exist in PipeWire, so it can be used to
    fill the gap for PipeWire warnings
