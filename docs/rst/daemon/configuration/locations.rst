.. _config_locations:

Locations of files
==================

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

   When the configuration directory is overriden with
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
overriden by the system administrator or the users.

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
