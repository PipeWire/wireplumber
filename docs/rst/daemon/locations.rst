.. _daemon_file_locations:

Locations of WirePlumber's files
================================

.. _config_locations:

Location of configuration files
-------------------------------

WirePlumber's default locations of its configuration files are the following,
in order of priority:

 1. ``$XDG_CONFIG_HOME/wireplumber``
 2. ``$XDG_CONFIG_DIRS/wireplumber``
 3. ``$sysconfdir/wireplumber``
 4. ``$XDG_DATA_DIRS/wireplumber``
 5. ``$datadir/wireplumber``

Notes:

 * ``$syscondir`` and ``$datadir`` refer to
   `meson's directory options <https://mesonbuild.com/Builtin-options.html#directories>`_
   and are hardcoded at build time
 * ``$XDG_`` variables refer to the
   `XDG Base Directory Specification <https://specifications.freedesktop.org/basedir-spec/latest/index.html>`_

It is recommended that user specific overrides are placed in
``$XDG_CONFIG_HOME/wireplumber``, while host-specific configuration is placed in
``$XDG_CONFIG_DIRS/wireplumber`` or ``$sysconfdir/wireplumber`` and
distribution-provided configuration is placed in ``$XDG_DATA_DIRS/wireplumber``
or ``$datadir/wireplumber``.

At runtime, WirePlumber will seek out the directory with the highest priority
that contains the required configuration file. This setup allows a user or
system administrator to effortlessly override the configuration files provided
by the distribution. They can achieve this by placing a file with an identical
name in a higher priority directory.

It is also possible to override the configuration directory by setting the
``WIREPLUMBER_CONFIG_DIR`` environment variable:

.. code-block:: bash

   WIREPLUMBER_CONFIG_DIR=src/config wireplumber

``WIREPLUMBER_CONFIG_DIR`` supports listing multiple directories, using the
standard path list separator ``:``. If multiple directories are specified,
the first one has the highest priority and the last one has the lowest.

.. note::

   When the configuration directory is overridden with
   ``WIREPLUMBER_CONFIG_DIR``, the default locations are ignored and
   configuration files are *only* looked up in the directories specified by this
   variable.

.. _config_locations_fragments:

Configuration fragments
^^^^^^^^^^^^^^^^^^^^^^^

WirePlumber also supports configuration fragments. These are configuration files
that are loaded in addition to the main configuration file, allowing to
override or extend the configuration without having to copy the whole file.
See also the :ref:`config_conf_file_fragments` section for semantics.

Configuration fragments are always loaded from subdirectories of the main search
directories that have the same name as the configuration file, with the ``.d``
suffix appended. For example, if WirePlumber loads ``wireplumber.conf``, it will
also load ``wireplumber.conf.d/*.conf``. Note also that the fragment files need
to have the ``.conf`` suffix.

When WirePlumber loads a configuration file from the default locations, it will
also load all configuration fragments that are present in all of the default
locations, but following the reverse order of priority. This allows
configuration fragments that are installed in more system-wide locations to be
overridden by the system administrator or the users.

For example, assuming WirePlumber loads ``wireplumber.conf``, from any of the
search locations, it will also locate and load the following fragments, in this
order:

 1. ``$datadir/wireplumber/wireplumber.conf.d/*.conf``
 2. ``$XDG_DATA_DIRS/wireplumber/wireplumber.conf.d/*.conf``
 3. ``$sysconfdir/wireplumber/wireplumber.conf.d/*.conf``
 4. ``$XDG_CONFIG_DIRS/wireplumber/wireplumber.conf.d/*.conf``
 5. ``$XDG_CONFIG_HOME/wireplumber/wireplumber.conf.d/*.conf``

Within each search location that contains fragments, the individual fragment
files are opened in alphanumerical order. This can be important to know, because
the parsing order matters in merging. See :ref:`config_conf_file_fragments`

.. note::

   When ``WIREPLUMBER_CONFIG_DIR`` is set, the default locations are ignored and
   fragment files are *only* looked up in the directories specified by this
   variable.

.. _config_locations_scripts:

Location of scripts
-------------------

WirePlumber's default locations of its data files are the following,
in order of priority:

 1. ``$XDG_DATA_HOME/wireplumber``
 2. ``$XDG_DATA_DIRS/wireplumber``
 3. ``$datadir/wireplumber``

At runtime, WirePlumber will search the directories for the highest-priority
directory to contain the needed data file.

Scripts are a specific kind of "data" files and are expected to be located
within a ``scripts`` subdirectory in the above data search locations. The "data"
directory is a somewhat more generic path that may be used for other kinds of
data files in the future.

It is also possible to override the data directory by setting the
``WIREPLUMBER_DATA_DIR`` environment variable:

.. code-block:: bash

   WIREPLUMBER_DATA_DIR=src wireplumber

As with the default data directories, script files in particular are expected
to be located within a ``scripts`` subdirectory, so in the above example the
scripts would actually reside in ``src/scripts``.

``WIREPLUMBER_DATA_DIR`` supports listing multiple directories, using the
standard path list separator ``:``. If multiple directories are specified,
the first one has the highest priority and the last one has the lowest.

.. note::

   When ``WIREPLUMBER_DATA_DIR`` is set, the default locations are ignored and
   scripts are *only* looked up in the directories specified by this variable.

Location of modules
-------------------

WirePlumber modules
^^^^^^^^^^^^^^^^^^^

WirePlumber's default location of its modules is
``$libdir/wireplumber-$api_version``, where ``$libdir`` is set at compile time
by the build system. Typically, it ends up being ``/usr/lib/wireplumber-0.5``
(or ``/usr/lib/<arch-triplet>/wireplumber-0.5`` on multiarch systems)

It is possible to override this directory at runtime by setting the
``WIREPLUMBER_MODULE_DIR`` environment variable:

.. code-block:: bash

   WIREPLUMBER_MODULE_DIR=build/modules wireplumber

``WIREPLUMBER_MODULE_DIR`` supports listing multiple directories, using the
standard path list separator ``:``. If multiple directories are specified, the
first one has the highest priority and the last one has the lowest.

.. note::

   When ``WIREPLUMBER_MODULE_DIR`` is set, the default locations are ignored and
   scripts are *only* looked up in the directories specified by this variable.

PipeWire and SPA modules
^^^^^^^^^^^^^^^^^^^^^^^^

PipeWire and SPA modules are not loaded from the same location as WirePlumber's
modules. They are loaded from the location that PipeWire loads them.

It is also possible to override these locations by using environment variables:
``SPA_PLUGIN_DIR`` and ``PIPEWIRE_MODULE_DIR``. For more details, refer to
PipeWire's documentation.

.. _state_locations:

Location of state files
-----------------------

Several components remember things across restarts: which node the user picked
as the default sink, the profile that was last selected on a device, the volume
of a stream, and so on. This information is not configuration, so it is not
looked up in the configuration directories. It is instead stored in a single,
per-user directory:

 * ``$XDG_STATE_HOME/wireplumber``

``$XDG_STATE_HOME`` defaults to ``~/.local/state``, as per the
`XDG Base Directory Specification <https://specifications.freedesktop.org/basedir-spec/latest/index.html>`_,
so in a typical setup this is ``~/.local/state/wireplumber``. The directory is
created automatically the first time something needs to be saved. Unlike the
locations described above, this is a single directory and not a search path,
and there is no WirePlumber-specific environment variable to override it;
setting ``$XDG_STATE_HOME`` moves it, together with the state directories of
all other applications.

Every state file is a plain text file in the ``.ini``-like *key file* format
and is named after the component that owns it. Each of them is written by a
single script, which is loaded by the feature listed below, and most of them
can additionally be switched off at runtime with a setting:

``bluetooth-autoswitch``
  The profile that each Bluetooth device had before it was automatically
  switched to a headset profile, so that it can be restored afterwards.

  :feature: ``hooks.device.profile.autoswitch-bluetooth``
  :settings: ``bluetooth.use-persistent-storage``

``default-nodes``
  The nodes that the user has selected as the default sink, source, etc.
  These selections take precedence over the automatic, priority-based
  selection; ``wpctl clear-default`` erases them.

  :feature: ``hooks.default-nodes.state``
  :settings: ``node.restore-default-targets``

``default-profile``
  The profile that the user has last selected on each device.

  :feature: ``hooks.device.profile.state``
  :settings: ``device.restore-profile``

``default-routes``
  The routes (ports) that the user has last selected on each device profile,
  together with their volume, mute and independent channel volumes.

  :feature: ``hooks.device.routes.state``
  :settings: ``device.restore-routes``

``sm-settings``
  Settings that were changed at runtime and saved with
  ``wpctl settings --save``. These override the values from the configuration
  file; see :ref:`config_configuration_option_types`.

  :feature: ``metadata.sm-settings``
  :settings: *none*

``stream-properties``
  The volume, mute state and target node of each stream, stored per
  application.

  :feature: ``hooks.stream.state``
  :settings: ``node.stream.restore-props``, ``node.stream.restore-target``

Disabling state storage
^^^^^^^^^^^^^^^^^^^^^^^

The features and the settings in the list above are two different mechanisms
and they work at different levels.

A **feature** determines whether the script that maintains the state file is
loaded at all. Disabling it in a profile is a static decision, taken when the
daemon starts, and it removes everything that the script does, which may be
more than just writing the file. For instance, disabling
``hooks.default-nodes.state`` also stops WirePlumber from remembering the
*previous* default node choices, which it normally uses to fall back to a
device that the user had preferred earlier, when the current default becomes
unavailable. See :ref:`config_components_and_profiles` for how to disable a
feature.

For convenience, the ``mixin.stateless`` profile disables all four features
that store the *policy* state in one go:

.. code-block:: none

   mixin.stateless = {
     hooks.device.profile.state = disabled
     hooks.device.routes.state = disabled
     hooks.default-nodes.state = disabled
     hooks.stream.state = disabled
   }

It is meant to be inherited by other profiles, as the built-in
``main-embedded`` profile does:

.. code-block:: none

   main-embedded = {
     inherits = [ main, mixin.systemwide-session, mixin.stateless ]
   }

This makes sense on embedded systems, which should always boot into a known
default state instead of remembering the changes that were made in the previous
boot. Note, however, that this mixin does not cover the two remaining state
files: ``bluetooth-autoswitch`` is controlled only by the
``bluetooth.use-persistent-storage`` setting, and ``sm-settings`` is written
whenever a setting is explicitly saved.

A **setting**, on the other hand, is a runtime switch. The scripts subscribe to
their settings and register or unregister their hooks on the fly, so state
storage can be turned on and off while the daemon is running, without
restarting it:

.. code-block:: bash

   $ wpctl settings --save node.stream.restore-props false

Unlike disabling the feature, this keeps the script loaded, so the parts of it
that do not involve persistence keep working. Turning a setting off also does
not delete the state file that has already been written; it only stops it from
being read and updated.

Resetting the state
^^^^^^^^^^^^^^^^^^^

Deleting the state directory brings WirePlumber back to a clean state: on the
next start, the components that read these files find nothing and fall back to
their defaults. Individual files may also be deleted, in order to forget only
one kind of state; removing ``stream-properties``, for example, resets the
volumes that were remembered per application, but leaves the default node and
device profile selections intact.

WirePlumber should not be running while the files are deleted, because the
components keep the state that they have loaded in memory and write it back out
whenever it changes, which would bring back the deleted entries.

The ``wpctl reset`` command takes care of this: it stops the daemon, deletes
``$XDG_STATE_HOME/wireplumber`` and starts the daemon again. It can optionally
delete the user's configuration files as well, and it can be asked to only
report what it would delete, without deleting anything:

.. code-block:: bash

   $ wpctl reset --dry-run

See :ref:`man_wpctl_reset` for the full list of options.
