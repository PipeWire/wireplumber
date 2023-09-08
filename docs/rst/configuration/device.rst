.. _config_device:

Device Configuration
====================

``wireplumber.conf.d/device.conf`` deals with the configuration, that is common
to all the devices. Interface specific configuration like
:ref:`ALSA<config_alsa>` , :ref:`Bluetooth<config_bluetooth>` etc is dealt in
separate config files.

Simple Configs
--------------

  All the :ref:`simple configs<configs_types>` can be
  :ref:`overridden<manipulate_config>` or can be changed
  :ref:`live<live_configs>`. They are commented in the default location, as they
  are built into WirePlumber. Below is the explanation of each of these simple
  configs.

  .. code-block::

    device.use-persistent-storage = true

  Enables storing/restoring device selection preferences(devices selected for
  audio/video playback, audio record etc), device
  profile preferences and device route preferences to the file system.

  When set to `false`, all the device selections are selected based on
  the inbuilt priorities and any runtime changes do not persist after restart.


  .. code-block::

    device.default-volume = 0.064

  Sets the device default volume level.

  .. code-block::

    device.save-interval-ms = 1000

  The persistent save interval in milliseconds when any change happens in a
  device config.
