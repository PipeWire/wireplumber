.. _daemon_multi_instance:

Running multiple instances
==========================

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

For multi-instance configuration, the default ``wireplumber.conf`` specifies 4
profiles:

.. describe:: policy

  This profile runs all the policy scripts, i.e. ones that monitor changes
  in the graph and execute actions to link nodes, select default devices,
  create new nodes or configure existing ones differently.

.. describe:: audio

  The audio profile runs the ALSA and ALSA MIDI monitors, which make audio &
  MIDI devices available to PipeWire.

.. describe:: bluetooth

  The bluetooth profile runs the BlueZ and BlueZ MIDI monitors, which enable
  Bluetooth audio & MIDI devices and other Bluetooth functionality tied to the
  A2DP, HSP, HFP and BAP profiles, using BlueZ.

.. describe:: video-capture

  The video-capture profile runs the V4L2 and libcamera monitors, which make
  video capture devices, such as cameras and HDMI capture cards, available
  to PipeWire.

.. note::

  The ``main`` profile includes all the functionality of the ``policy``,
  ``audio``, ``video-capture`` and ``bluetooth`` profiles combined (i.e. it is
  the default for a standard single instance configuration). You should never
  load the ``main`` profile alongside these other 4 profiles, as their
  functionality will conflict.

.. warning::

  Always ensure that the instances you load serve a different purpose and they
  do not conflict with each other. Conflicting components executed in parallel
  will have undefined behavior.

Systemd integration
-------------------

To make this easier to work with, a template systemd unit is provided, which is
meant to be started with the name of the profile as a template argument:

.. code-block:: console

  $ systemctl --user disable wireplumber # disable the "main" profile instance
  $ systemctl --user enable wireplumber@policy
  $ systemctl --user enable wireplumber@audio
  $ systemctl --user enable wireplumber@video-capture
  $ systemctl --user enable wireplumber@bluetooth

.. note::

   In WirePlumber 0.4, the template argument was the name of the configuration
   file to load, since profiles did not exist. In WirePlumber 0.5, the template
   argument is the name of the profile and the configuration file is always
   ``wireplumber.conf``. To change the name of the configuration file you need
   to craft custom systemd unit files and use the ``--config-file`` command line
   option as needed.
