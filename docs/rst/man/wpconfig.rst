.. _man_wpconfig:

wpconfig(1)
===========

SYNOPSIS
--------

| **wpconfig** [*OPTIONS*] [**paths**]
| **wpconfig** [*OPTIONS*] **list** [*SECTION*]
| **wpconfig** [*OPTIONS*] **merge** [*SECTION*]

DESCRIPTION
-----------

**wpconfig** inspects WirePlumber's static configuration. It uses the same
file lookup, fragment loading, merging and ``override.`` rules as
WirePlumber. It does not connect to PipeWire or to a running WirePlumber
process.

With no command, **wpconfig** runs **paths**.

COMMANDS
--------

paths
^^^^^

Print the absolute path of each successfully loaded configuration file in
processing order. This includes readable empty files. Files that cannot be
read or parsed are not included.

list
^^^^

**wpconfig list** [*SECTION*]

Print original, unmerged section definitions as an array. Each item contains
``section.name``, ``section.location`` and ``section.value``. Repeated section
names remain separate and retain configuration load order.

When *SECTION* is given, show only definitions that contribute to that logical
section. For example, ``list foo`` includes both ``foo`` and ``override.foo``
definitions while preserving their source names.

merge
^^^^^

**wpconfig merge** [*SECTION*]

With *SECTION*, print its effective value after applying WirePlumber's normal
merge rules.

Without *SECTION*, print one top-level object containing every logical section
exactly once. Section names have no synthetic ``override.`` prefix, and each
value is obtained through the same merge operation used by WirePlumber.

OPTIONS
-------

**-h**, **--help**
  Show command help and exit.

**-V**, **--version**
  Show the tool, compile-time and linked WirePlumber versions and exit.

**-n** *NAME*, **--name=**\ *NAME*
  Select the configuration filename. The default is ``wireplumber.conf``. An
  absolute filename uses the normal absolute-path behavior of WirePlumber's
  configuration lookup.

**-L**, **--no-newline**
  Use compact output without formatting newlines between values.

**-r**, **--recurse**
  Recursively reformat nested JSON containers.

**-N**, **--no-colors**
  Disable ANSI colors.

**-C**\ [*WHEN*], **--color**\ [**=**\ *WHEN*]
  Control ANSI colors. *WHEN* is ``auto``, ``always`` or ``never``. The
  default is ``auto``.

**-s**, **--spa**
  Use SPA JSON style, including ``=`` between object keys and values and no
  comma separators.

ENVIRONMENT
-----------

**WIREPLUMBER_CONFIG_DIR**
  Select an alternate WirePlumber configuration directory using the standard
  WirePlumber lookup rules.

**NO_COLOR**
  Disable automatic color output when this variable is present. An explicit
  ``--color=always`` overrides it.

**WIREPLUMBER_DEBUG**
  Set WirePlumber library logging. Without this variable, **wpconfig** only
  enables warning and error diagnostics.

EXAMPLES
--------

List the files that make up the default configuration::

  wpconfig paths

Show every source definition of an ALSA rules section::

  wpconfig -r list monitor.alsa.rules

Show the effective ALSA rules section::

  wpconfig -r merge monitor.alsa.rules

Show the complete effective configuration in compact SPA JSON form::

  wpconfig -s -r -L merge

Inspect a configuration by absolute filename::

  wpconfig -n /tmp/wireplumber.conf merge

EXIT STATUS
-----------

**0**
  Success.

**1**
  Configuration loading, output or internal data processing failed.

**2**
  Command-line usage error.

SEE ALSO
--------

**wireplumber**\ (1), **wpctl**\ (1), **pw-config**\ (1)

WirePlumber Documentation: https://pipewire.pages.freedesktop.org/wireplumber/
