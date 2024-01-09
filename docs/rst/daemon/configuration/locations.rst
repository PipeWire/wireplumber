.. _config_locations:

Locations of files
==================

Location of configuration files
-------------------------------

WirePlumber's default locations of its configuration files are the same as
pipewire's. Typically, those end up being ``$XDG_CONFIG_HOME/pipewire``,
``/etc/pipewire``, and ``/usr/share/pipewire``, in that order of priority.

.. note::

   Starting with WirePlumber 0.5, the configuration files are located in
   the ``pipewire`` directory. In previous versions they used to be in the
   ``wireplumber`` directory.

The three designated locations are purposed for custom user configuration,
host-specific configuration, and distribution-provided configuration,
respectively. At runtime, WirePlumber will seek out the directory with the
highest priority that contains the required configuration file. This setup
allows a user or system administrator to effortlessly override the configuration
files provided by the distribution. They can achieve this by placing a file with
an identical name in a higher priority directory.

It is also possible to override the configuration directory by setting the
``WIREPLUMBER_CONFIG_DIR`` environment variable:

.. code-block:: bash

   WIREPLUMBER_CONFIG_DIR=src/config wireplumber

This is the same as the ``PIPEWIRE_CONFIG_DIR`` environment variable, which has
the same effect. But for convenience, WirePlumber also supports the
``WIREPLUMBER_CONFIG_DIR`` environment variable. When the
``WIREPLUMBER_CONFIG_DIR`` environment variable is set, the
``PIPEWIRE_CONFIG_DIR`` environment variable is ignored.

When the configuration directory is overriden with ``WIREPLUMBER_CONFIG_DIR`` or
``PIPEWIRE_CONIFG_DIR``, the default locations are ignored and configuration
files are *only* looked up in this directory.

Configuration fragments
^^^^^^^^^^^^^^^^^^^^^^^

WirePlumber also supports configuration fragments. These are configuration files
that are loaded in addition to the main configuration file, allowing to
override or extend the configuration without having to copy the whole file.
See also the :ref:`config_conf_file` section for semantics.

Configuration fragments are always loaded from subdirectories of the main search
directories that have the same name as the configuration file, with the ``.d``
suffix appended. For example, if WirePlumber loads ``wireplumber.conf``, it will
also load ``wireplumber.conf.d/*.conf``. Note also that the fragment files need
to have the ``.conf`` suffix.

When WirePlumber loads a configuration file from the default locations, it will
also load all configuration fragments that are present in any of the search
directories in the default locations. For example, if the main configuration
file is ``/etc/pipewire/wireplumber.conf``, WirePlumber will also load
``$XDG_CONFIG_HOME/pipewire/wireplumber.conf.d/*.conf`` and
``/etc/pipewire/wireplumber.conf.d/*.conf`` and
``/usr/share/pipewire/wireplumber.conf.d/*.conf``, in that order. It does not
matter where the main configuration file was loaded from.

However, when WirePlumber loads a configuration file from a directory specified
via ``WIREPLUMBER_CONFIG_DIR`` or ``PIPEWIRE_CONFIG_DIR``, it will only load
configuration fragments from that directory.

Location of scripts
-------------------

WirePlumber's default locations of its scripts are similar as the ones for the
configuration files, but they reside in ``wireplumber/scripts/``, relative to
the base path, unlike the configuration files which reside in ``pipewire/``.
Typically, these end up being ``$XDG_CONFIG_HOME/wireplumber/scripts``,
``/etc/wireplumber/scripts``, and ``/usr/share/wireplumber/scripts``,
in that order of priority.

The three locations are intended for custom user scripts,
host-specific scripts and distribution-provided scripts, respectively.
At runtime, WirePlumber will search the directories for the highest-priority
directory to contain the needed script.

It is also possible to override the scripts directory by setting the
``WIREPLUMBER_DATA_DIR`` environment variable:

.. code-block:: bash

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
``/usr/lib/wireplumber-0.5`` (or ``/usr/lib/<arch-triplet>/wireplumber-0.5`` on
multiarch systems)

In more detail, this is controlled by the ``--libdir`` meson option. When
this is set to an absolute path, such as ``/lib``, the location of the
modules is set to be ``$libdir/wireplumber-$abi_version``. When this is set
to a relative path, such as ``lib``, then the installation prefix (``--prefix``)
is prepended to the path: ``$prefix/$libdir/wireplumber-$abi_version``.

It is possible to override this directory at runtime by setting the
``WIREPLUMBER_MODULE_DIR`` environment variable:

.. code-block:: bash

   WIREPLUMBER_MODULE_DIR=build/modules wireplumber

PipeWire and SPA modules
^^^^^^^^^^^^^^^^^^^^^^^^

PipeWire and SPA modules are not loaded from the same location as WirePlumber's
modules. They are loaded from the location that PipeWire loads them.

It is also possible to override these locations by using environment variables:
``SPA_PLUGIN_DIR`` and ``PIPEWIRE_MODULE_DIR``. For more details, refer to
PipeWire's documentation.
