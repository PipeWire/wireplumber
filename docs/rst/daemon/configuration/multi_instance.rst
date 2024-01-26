.. _config_multi_instance:

Running multiple instances
==========================

.. warning::

   Multi-instance mode has not been extensively tested in 0.5.0, so it is
   possible that some features are not working as expected. An update on this is
   planned for a subsequent 0.5.x release.

WirePlumber has the ability to run either as a single instance daemon or as
multiple instances, meaning that there can be multiple processes, each one
doing a different task.

The most common use case for such a setup is to separate the graph orchestration
tasks from the device monitoring and object creation ones. This can be useful
for robustness and security reasons, as it allows restarting the device monitors
or running them in different security contexts without affecting the rest of the
session management functionality.

To achieve a multi-instance setup, WirePlumber can be started multiple times
with a different :ref:`profile<config_components_and_profiles>` loaded in each
instance. This can be achieved using the ``--profile`` command line option to
select the profile to load:

.. code-block:: console

  $ wireplumber --profile=custom

When no particular profile is specified, the ``main`` profile is loaded.

Systemd integration
-------------------

To make this easier to work with, a template systemd unit is provided, which is
meant to be started with the name of the profile as a template argument:

.. code-block:: console

  $ systemctl --user disable wireplumber # disable the "main" instance

  $ systemctl --user enable wireplumber@policy
  $ systemctl --user enable wireplumber@audio
  $ systemctl --user enable wireplumber@camera
  $ systemctl --user enable wireplumber@bluetooth

.. note::

   In WirePlumber 0.4, the template argument was the name of the configuration
   file to load, since profiles did not exist. In WirePlumber 0.5, the template
   argument is the name of the profile and the configuration file is always
   ``wireplumber.conf``. To change the name of the configuration file you need
   to craft custom systemd unit files and use the ``--config-file`` command line
   option as needed.

It is obviously possible to start as many instances as desired, with manually
crafted profiles, as long as it is ensured that these instances
serve a different purpose and they do not conflict with each other.
