.. _config_locations:

Locations of files
==================

Location of configuration files
-------------------------------

WirePlumber's default locations of its configuration files are the same as
pipewire. Typically, those end up being
``$XDG_CONFIG_DIR/pipewire``, ``/etc/pipewire``, and
``/usr/share/pipewire``, in that order of priority.

The three locations are intended for custom user configuration,
host-specific configuration and distribution-provided configuration,
respectively. At runtime, WirePlumber will search the directories
for the highest-priority directory to contain the needed configuration file.
This allows a user or system administrator to easily override the distribution
provided configuration files by placing an equally named file in the respective
directory.

It is also possible to override the configuration directory by setting the
``WIREPLUMBER_CONFIG_DIR`` environment variable::

  WIREPLUMBER_CONFIG_DIR=src/config wireplumber

For convenience, the behaviour of the ``WIREPLUMBER_CONFIG_DIR`` environment
variable is the same as the ``PIPEWIRE_CONFIG_DIR`` environment variable.
If ``WIREPLUMBER_CONFIG_DIR`` is set, the default locations are ignored and
configuration files are *only* looked up in this directory.


Location of scripts
-------------------

WirePlumber's default locations of its scripts are the same ones as for the
configuration files, but with the ``scripts`` directory appended.
Typically, these end up being ``$XDG_CONFIG_DIR/wireplumber/scripts``,
``/etc/wireplumber/scripts``, and ``/usr/share/wireplumber/scripts``,
in that order of priority.

The three locations are intended for custom user scripts,
host-specific scripts and distribution-provided scripts, respectively.
At runtime, WirePlumber will search the directories for the highest-priority
directory to contain the needed script.

It is also possible to override the scripts directory by setting the
``WIREPLUMBER_DATA_DIR`` environment variable::

  WIREPLUMBER_DATA_DIR=src wireplumber

The "data" directory is a somewhat more generic path that may be used for
other kinds of data files in the future. For scripts, WirePlumber still expects
to find a ``scripts`` subdirectory in this "data" directory, so in the above
example the scripts would be in ``src/scripts``.

If ``WIREPLUMBER_DATA_DIR`` is set, the default locations are ignored and
scripts are *only* looked up in this directory.

Location of modules
-------------------

WirePlumber modules
^^^^^^^^^^^^^^^^^^^

Like with configuration files, WirePlumber's default location of its modules is
determined at compile time by the build system. Typically, it ends up being
``/usr/lib/wireplumber-0.4`` (or ``/usr/lib/<arch-triplet>/wireplumber-0.4`` on
multiarch systems)

In more detail, this is controlled by the ``--libdir`` meson option. When
this is set to an absolute path, such as ``/lib``, the location of the
modules is set to be ``$libdir/wireplumber-$abi_version``. When this is set
to a relative path, such as ``lib``, then the installation prefix (``--prefix``)
is prepended to the path: ``$prefix/$libdir/wireplumber-$abi_version``.

It is possible to override this directory at runtime by setting the
``WIREPLUMBER_MODULE_DIR`` environment variable::

  WIREPLUMBER_MODULE_DIR=build/modules wireplumber

PipeWire and SPA modules
^^^^^^^^^^^^^^^^^^^^^^^^

PipeWire and SPA modules are not loaded from the same location as WirePlumber's
modules. They are loaded from the location that PipeWire loads them.

It is also possible to override these locations by using environment variables:
``SPA_PLUGIN_DIR`` and ``PIPEWIRE_MODULE_DIR``. For more details, refer to
PipeWire's documentation.
