wireplumber(1)
==============

SYNOPSIS
--------

**wireplumber** [**-c**\ \|\ **--config-file**\ =\ *FILE*]
[**-p**\ \|\ **--profile**\ =\ *PROFILE*] [**-v**\ \|\ **--version**]

DESCRIPTION
-----------

**wireplumber** is the WirePlumber daemon, a modular session and policy manager
for PipeWire. By itself it does nothing except load the components listed in
the selected profile; all the actual management logic — bringing up devices,
linking streams, granting permissions to clients — is implemented in those
components.

It is normally started as a systemd user service rather than by hand:

.. code-block:: console

   $ systemctl --user status wireplumber

OPTIONS
-------

**-c**, **--config-file**\ =\ *FILE*
  Use *FILE* as the main configuration file instead of ``wireplumber.conf``.
  The corresponding ``FILE.d/`` fragment directory is loaded as well.

**-p**, **--profile**\ =\ *PROFILE*
  Load *PROFILE* instead of the default ``main`` profile. This is how a second
  instance is started with only part of the functionality; see
  :ref:`daemon_multi_instance`.

**-v**, **--version**
  Print the version and exit.

ENVIRONMENT
-----------

**WIREPLUMBER_DEBUG**
  Sets the log level and per-topic log levels. See :ref:`daemon_logging`.

**WIREPLUMBER_CONFIG_DIR**
  Overrides the directory that is searched first for configuration files.

**WIREPLUMBER_DATA_DIR**
  Overrides the directory that is searched first for data files, such as
  scripts.

**WIREPLUMBER_MODULE_DIR**
  Overrides the directory that is searched first for modules.

See :ref:`daemon_file_locations` for the full search order of each of these.

FILES
-----

Configuration files are looked up in the following directories, listed here
from the highest to the lowest priority:

*$XDG_CONFIG_HOME*\ ``/wireplumber/``
  User configuration; ``~/.config/wireplumber/`` unless *$XDG_CONFIG_HOME* is
  set.

*$XDG_CONFIG_DIRS*\ ``/wireplumber/``
  Host-specific configuration; ``/etc/xdg/wireplumber/`` unless
  *$XDG_CONFIG_DIRS* is set.

``/etc/wireplumber/``
  Host-specific configuration, in the compile-time *$sysconfdir*.

*$XDG_DATA_DIRS*\ ``/wireplumber/``
  Distribution-provided configuration; ``/usr/local/share/wireplumber/`` and
  ``/usr/share/wireplumber/`` unless *$XDG_DATA_DIRS* is set.

``/usr/share/wireplumber/``
  Configuration that ships with WirePlumber, in the compile-time *$datadir*.

In each of these directories, ``wireplumber.conf`` is the main configuration
file and ``wireplumber.conf.d/*.conf`` are fragments that are merged into it.
Only the first ``wireplumber.conf`` that is found is loaded, but fragments are
loaded from *all* the directories, in reverse order of priority, so that a
fragment in a higher priority directory overrides the ones below it.

Scripts and other data files are looked up in a ``scripts`` subdirectory of the
following directories, again from the highest to the lowest priority:

*$XDG_DATA_HOME*\ ``/wireplumber/``
  User-provided scripts; ``~/.local/share/wireplumber/`` unless
  *$XDG_DATA_HOME* is set.

*$XDG_DATA_DIRS*\ ``/wireplumber/``
  Host-specific scripts; ``/usr/local/share/wireplumber/`` and
  ``/usr/share/wireplumber/`` unless *$XDG_DATA_DIRS* is set.

``/usr/share/wireplumber/``
  The scripts that ship with WirePlumber, in the compile-time *$datadir*.

The remaining locations are not searched in any order:

``/usr/lib/wireplumber-0.5/``
  WirePlumber modules, in the compile-time *$libdir*. On multiarch systems this
  is ``/usr/lib/``\ *<arch-triplet>*\ ``/wireplumber-0.5/``. PipeWire and SPA
  modules are loaded from PipeWire's own module directories instead.

*$XDG_STATE_HOME*\ ``/wireplumber/``
  Saved state: default nodes, device profiles and routes, stream properties and
  settings that were changed at runtime. This is
  ``~/.local/state/wireplumber/`` unless *$XDG_STATE_HOME* is set.

EXIT STATUS
-----------

Exit codes follow the conventions of **sysexits.h**\ (3head):

**0** (*EX_OK*)
  Success.

**64** (*EX_USAGE*)
  Command line usage error.

**69** (*EX_UNAVAILABLE*)
  A required service was unavailable; typically WirePlumber could not connect
  to PipeWire.

**70** (*EX_SOFTWARE*)
  Internal software error.

**78** (*EX_CONFIG*)
  Configuration error.

SEE ALSO
--------

**wpctl**\ (1), **wpexec**\ (1), **pipewire**\ (1), **pw-cli**\ (1),
**pw-dump**\ (1), **sysexits.h**\ (3head)

WirePlumber Documentation: https://pipewire.pages.freedesktop.org/wireplumber/
