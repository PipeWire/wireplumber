.. _daemon_getting_started:

Getting started
===============

If your distribution ships WirePlumber — which most do — it is probably already
installed and running as part of your PipeWire setup. This page is a short tour
of how to check that, what you can do from the command line, and where to go
next.

If you want to build WirePlumber from source instead, see
:ref:`daemon_installing`.

Is it running?
--------------

On a systemd-based system:

.. code-block:: console

   $ systemctl --user status wireplumber

If it is not running, enable and start it:

.. code-block:: console

   $ systemctl --user --now enable wireplumber

See :ref:`daemon_running` for non-systemd systems and for running WirePlumber
by hand.

First commands
--------------

``wpctl`` is the command-line control tool. Start with an overview of the
graph:

.. code-block:: console

   $ wpctl status

This lists the devices, sinks, sources and streams that WirePlumber currently
manages, each with an id. Those ids are what the other commands take.

.. code-block:: console

   # change the volume of the default sink
   $ wpctl set-volume @DEFAULT_SINK@ 50%

   # mute and unmute it
   $ wpctl set-mute @DEFAULT_SINK@ toggle

   # make a different device the default
   $ wpctl set-default 47

   # look at everything WirePlumber knows about an object
   $ wpctl inspect 47

See :ref:`wpctl(1) <man_wpctl>` for the full list of commands.

Changing the configuration
--------------------------

WirePlumber's own configuration lives in ``/usr/share/wireplumber/``. Do not
edit it — it belongs to the package and will be overwritten on upgrade.

Instead, drop a *fragment* into your own configuration directory, containing
only the settings you want to change:

.. code-block:: bash

   $ mkdir -p ~/.config/wireplumber/wireplumber.conf.d
   $ $EDITOR ~/.config/wireplumber/wireplumber.conf.d/50-my-config.conf

Fragments are merged on top of the shipped configuration, so you only need to
write the parts you are changing. See
:ref:`config_modifying_configuration` for the details, and
:ref:`config_example_fragments` for ready-made fragments you can copy and
uncomment.

Many options can also be changed at runtime, without editing any file:

.. code-block:: console

   $ wpctl settings

Getting logs
------------

.. code-block:: console

   $ journalctl --user -u wireplumber

To get more detail, raise the log level. This can be done at runtime:

.. code-block:: console

   $ wpctl set-log-level D

or for a single run, with an environment variable that also lets you select
which parts of WirePlumber you are interested in:

.. code-block:: console

   $ WIREPLUMBER_DEBUG=s-linking:D wireplumber

See :ref:`daemon_logging` for the full syntax and the list of topics.

Where to go next
----------------

- :ref:`daemon_configuration` — the configuration file format, and the
  per-subsystem options for ALSA, Bluetooth, video and access control.
- :ref:`design_understanding_wireplumber` — how WirePlumber is put together, if you
  want to know what is actually happening.
- :ref:`scripting_custom_scripts` — writing your own policy in Lua.
- :ref:`resources_community` — where to ask questions and report bugs.
