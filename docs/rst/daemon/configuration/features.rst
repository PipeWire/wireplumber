.. _config_features:

Well-known features
===================

This is a list of some well-known features that can be enabled or
disabled accordingly.

There are many more features actually defined in the configuration file, and it
can be confusing to go through them. This list here is meant to be a quick
reference for the most common ones that actually make sense to be toggled in
a configuration file in order to customize WirePlumber's behavior.

Hardware monitors
-----------------

Audio
~~~~~

.. describe:: hardware.audio

   Enables bringing up audio hardware.

   :wants: ``monitor.alsa``, ``monitor.alsa-midi``

.. describe:: monitor.alsa

   Enables the ALSA device monitor.

   :wants: ``monitor.alsa.reserve-device``

.. describe:: monitor.alsa.reserve-device

   Enables D-Bus device reservation API for ALSA devices.

   :requires: ``support.reserve-device``

.. describe:: monitor.alsa-midi

   Enables the ALSA MIDI device monitor.

Bluetooth
~~~~~~~~~

.. describe:: hardware.bluetooth

   Enables bringing up bluetooth hardware.

   :wants: ``monitor.bluez``, ``monitor.bluez-midi``

.. describe:: monitor.bluez

   Enables the BlueZ device monitor.

   :wants: ``monitor.bluez.seat-monitoring``

.. describe:: monitor.bluez.seat-monitoring

   Enables seat monitoring on the bluetooth monitor.

   When enabled, this will make sure that the bluetooth devices are only
   enabled on the active seat.

   :requires: ``support.logind``

.. describe:: monitor.bluez-midi

   Enables the BlueZ MIDI device monitor.

   :wants: ``monitor.bluez.seat-monitoring``

Video
~~~~~

.. describe:: hardware.video-capture

   Enables bringing up video capture hardware (cameras, hdmi capture devices,
   etc.)

   :wants: ``monitor.v4l2``, ``monitor.libcamera``

.. describe:: monitor.v4l2

   Enables the V4L2 device monitor.

.. describe:: monitor.libcamera

   Enables the libcamera device monitor.

Support components
------------------

.. describe:: support.dbus

   Provides a D-Bus connection to the session bus. This is needed by some other
   support features (see below) but it is generally optional. WirePlumber does
   not require a D-Bus connection to work.

   On a system where WirePlumber is configured to run system-wide (headless,
   embedded, etc), this will most likely fail to load and thus disable all the
   other support features that require it. On such systems it makes sense to
   disable this feature explicitly, to avoid the overhead of trying to connect
   to the session bus.

.. describe:: support.reserve-device

   Provides support for the
   `D-Bus device reservation API <http://git.0pointer.net/reserve.git/tree/reserve.txt>`_,
   allowing the device monitors to reserve devices for exclusive access.

   :requires: ``support.dbus``

.. describe:: support.portal-permissionstore

   Integrates with the flatpak portal permission store to give appropriate
   access permissions to flatpak applications.

   :requires: ``support.dbus``

.. describe:: support.logind

   Integrates with systemd-logind to enable specific functionality only on the
   active seat.

Policies
--------

.. describe:: policy.standard

   Enables the standard WirePlumber policy. This includes all the logic
   for enabling devices, linking streams, granting permissions to clients,
   etc, as appropriate for a desktop system.

.. describe:: policy.role-priority-system

   Enables the role priority system policy. This system creates virtual sinks
   that group streams based on their ``media.role`` property, and assigns a
   priority to each role. Depending on the priority configuration, lower
   priority roles may be corked or ducked when a higher priority role stream
   is active.

   This policy was designed for automotive and mobile systems and may not work
   as expected on desktop systems.

   Note that this policy is implemented as a superset of ``policy.standard``,
   so ``policy.standard`` should not be disabled when enabling this policy.

   :requires: ``policy.standard``
