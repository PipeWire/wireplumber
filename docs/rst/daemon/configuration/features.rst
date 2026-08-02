.. _config_features:

Well-known features
===================

This is a list of some well-known features that can be enabled or
disabled accordingly.

There are many more features actually defined in the configuration file, and it
can be confusing to go through them. This list here is meant to be a quick
reference for the most common ones that actually make sense to be toggled in
a configuration file in order to customize WirePlumber's behavior.

For more information on what features are and how they work, refer to the
previous section: :ref:`config_components_and_profiles`.

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

.. describe:: node.software-dsp

   Enables software DSP based on pre-configured hardware rules.

   See :ref:`policies_software_dsp` for more information.

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

.. describe:: support.client-context

   Starts a secondary ``pw_context``, running on its own thread, which hosts
   every PipeWire object that WirePlumber implements in its own process: the
   PipeWire modules loaded as ``pw-module-client`` components or from Lua with
   ``LocalModule()``, the nodes created with ``LocalNode()`` and the SPA device
   handles wrapped by ``SpaDevice()``.

   WirePlumber's main loop also dispatches the event dispatcher, whose hooks
   can run for a long time, and GSource priorities do not preempt: anything
   sharing that loop makes no progress for as long as a hook runs. In-process
   media objects (i.e. local nodes and modules) should be given their own loop
   to be able to receive media commands without delays, or they may lose audio.

   This is enabled by default and there is rarely a reason to disable it. If
   it is disabled, those objects fall back to WirePlumber's main
   ``pw_context`` and become subject to the delays described above.

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

.. describe:: support.modem-manager

   Integrates with ModemManager to track the state of voice calls, so that a
   "Voice Call" device profile can be selected while a call is in progress.

   :requires: ``support.system-dbus``

.. describe:: support.mpris

   Provides MPRIS media player control. This is what allows WirePlumber to
   pause media players when the sink they are playing to disappears; see the
   ``linking.pause-playback`` setting in :ref:`config_settings`.

   :requires: ``support.dbus``

.. describe:: support.session-services

   Announces which session management services this WirePlumber instance
   actually provides, by setting the ``session.services`` property on its
   PipeWire client object. This is how other software can tell what a given
   instance is responsible for, which matters in a
   :ref:`multi-instance setup <daemon_multi_instance>`.

.. describe:: api.notifications

   Provides an API for sending desktop notifications through
   ``org.freedesktop.Notifications``. It is used, for instance, to notify the
   user when devices are auto-muted.

   :requires: ``support.dbus``

.. describe:: metadata.sm-objects

   Creates the ``sm-objects`` metadata, through which clients can ask
   WirePlumber to load and unload objects — PipeWire modules, session items and
   so on — at runtime.

Nodes
-----

.. describe:: node.audio-group

   Groups together the streams of processes that share a common ancestor,
   placing them behind a single loopback filter so that they can be controlled
   as one.

.. describe:: hooks.filter.graph

   Enables attaching an internal filter graph to a node, based on the
   ``node.filter-graph.rules`` configuration section, without exposing extra
   nodes in the graph.

   The ``filter-graph.conf`` example fragment demonstrates this; see
   :ref:`config_example_fragments`.

Policies
--------

.. describe:: policy.standard

   Enables the standard WirePlumber policy. This includes all the logic
   for enabling devices, linking streams, granting permissions to clients,
   etc, as appropriate for a desktop system.

.. describe:: policy.linking.role-based

   Enables the role based priority system policy. This system creates virtual sinks
   that group streams based on their ``media.role`` property, and assigns a
   priority to each role. Depending on the priority configuration, lower
   priority roles may be corked or ducked when a higher priority role stream
   is active.

   This policy was designed for automotive and mobile systems and may not work
   as expected on desktop systems.

   Note that this policy is implemented as a superset of ``policy.standard``,
   so ``policy.standard`` should not be disabled when enabling this policy.

   :requires: ``policy.standard``
