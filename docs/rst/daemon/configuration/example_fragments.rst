.. _config_example_fragments:

Example configuration fragments
===============================

WirePlumber ships a set of ready-made, fully commented configuration fragments
that cover the sections most users want to change. They are the fastest way to
get started: every option is present but commented out, together with an
explanation of what it does.

They are installed under::

   $PREFIX/share/doc/wireplumber/examples/wireplumber.conf.d/

To use one, copy it into your own configuration directory and uncomment the
parts you want:

.. code-block:: bash

   $ mkdir -p ~/.config/wireplumber/wireplumber.conf.d
   $ cp /usr/share/doc/wireplumber/examples/wireplumber.conf.d/alsa.conf \
        ~/.config/wireplumber/wireplumber.conf.d/
   $ $EDITOR ~/.config/wireplumber/wireplumber.conf.d/alsa.conf

See :ref:`config_modifying_configuration` for how fragments are merged with the
main configuration file, and :ref:`config_locations` for the other directories
that are searched.

.. note::

   Copying a fragment as-is changes nothing, since everything in it is
   commented out. This is deliberate — it means you can drop the file in place
   and then uncomment options one at a time.

Available fragments
-------------------

=========================== ====================================================
File                        Contains
=========================== ====================================================
``access.conf``             Client access control: the
                            ``access.permission-managers`` chain and
                            ``access.rules``. See :ref:`config_access`.
``alsa.conf``               ALSA monitor properties and rules, ALSA MIDI
                            properties, and the ALSA-related settings.
                            See :ref:`config_alsa`.
``bluetooth.conf``          BlueZ monitor properties and rules, BlueZ MIDI
                            properties, servers and rules, and the Bluetooth
                            settings. See :ref:`config_bluetooth`.
``device.conf``             Device-related settings: profile and route
                            restoring, default volumes, auto-mute.
``filter-graph.conf``       ``node.filter-graph.rules``, for attaching an
                            internal filter graph to a node.
``libcamera.conf``          libcamera monitor properties and rules.
                            See :ref:`config_video`.
``linking.conf``            Linking policy settings.
``log.conf``                The default log level, set through
                            ``context.properties``. See :ref:`daemon_logging`.
``media-role-nodes.conf``   Loopback nodes for the role-based linking policy,
                            with the components, profile and settings needed to
                            enable it.
``profile.conf``            Pins WirePlumber to a specific profile through
                            ``context.properties``. Note that this overrides
                            the ``--profile`` command line option.
``smart-equalizer.conf``    A complete example of a smart filter: an equalizer
                            built with ``filter-chain``.
                            See :ref:`policies_smart_filters`.
``stream.conf``             ``stream.rules`` and the stream-related settings.
``v4l2.conf``               V4L2 monitor properties and rules.
                            See :ref:`config_video`.
=========================== ====================================================

Active fragments
----------------

Separately from the examples above, WirePlumber installs a few fragments into
its own configuration directory, at
``$PREFIX/share/wireplumber/wireplumber.conf.d/``. These are not examples;
they are part of the shipped configuration.

=============================== ================================================
File                            Effect when enabled
=============================== ================================================
``alsa-vm.conf``                ALSA property overrides applied when running
                                inside a virtual machine.
=============================== ================================================
