.. _config_multi_instance:

Running multiple instances
==========================

WirePlumber has the ability to run either as a single instance daemon or as
multiple instances, meaning that there can be multiple processes, each one
doing a different task.

In the default configuration, both setups are supported. The default is to run
in single-instance mode.

In single-instance mode, WirePlumber reads ``wireplumber.conf``, which is the
default configuration file, and from there it loads ``main.lua``, ``policy.lua``
and ``bluetooth.lua``, which are lua configuration files (deployed as directories)
that enable all the relevant functionality.

In multi-instance mode, WirePlumber is meant to be started with the
``--config-file`` command line option 3 times:

.. code-block:: console

  $ wireplumber --config-file=main.conf
  $ wireplumber --config-file=policy.conf
  $ wireplumber --config-file=bluetooth.conf

That loads one process which reads ``main.conf``, which then loads ``main.lua``
and enables core functionality. Then another process that reads ``policy.conf``,
which then loads ``policy.lua`` and enables policy functionality... and so on.

To make this easier to work with, a template systemd unit is provided, which is
meant to be started with the name of the main configuration file as a
template argument:

.. code-block:: console

  $ systemctl --user disable wireplumber # disable the single instance

  $ systemctl --user enable wireplumber@main
  $ systemctl --user enable wireplumber@policy
  $ systemctl --user enable wireplumber@bluetooth

It is obviously possible to start as many instances as desired, with manually
crafted configuration files, as long as it is ensured that these instances
serve a different purpose and they do not conflict with each other.
