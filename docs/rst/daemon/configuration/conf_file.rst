.. _config_conf_file:

Configuration file
==================

WirePlumber's configuration file is by default ``wireplumber.conf`` and resides
in the ``pipewire`` configuration directory (see :ref:`config_locations` for
more details on that).

The default configuration file can be changed on the command line by passing
the ``--config-file`` or ``-c`` option:

.. code-block:: bash

   $ wireplumber --config-file=custom.conf

.. note::

   Starting with WirePlumber 0.5, this is the only file that WirePlumber reads
   to load configuration (together with its fragments - see below). In the past,
   WirePlumber also used to read Lua configuration files that were referenced
   from ``wireplumber.conf`` and all the heavy lifting was done in Lua. This is
   no longer the case, and the Lua configuration files are no longer supported.

   Note that Lua is still the scripting language for WirePlumber, but it is only
   used for actual scripting and not for configuration.

Format
------

The format of this configuration file is a variant of JSON that is also
used in PipeWire configuration files (also known as SPA-JSON). The file consists
of a global JSON object that is not explicitly typed, and a list of sections
which are essentially key-value pairs of that global JSON object. Each section
is usually a JSON object, but it can also be a JSON array.

SPA-JSON is a superset of standard JSON, so any valid JSON file is also a valid
SPA-JSON file. However, it is more permissive than standard JSON. First of all,
it allows strings to be typed without quotes (``"``), and it also allows the
character ``=`` as a separator between keys and values in addition to the
standard ``:``. This can make it look similar to INI files or other custom
configuration formats that people are familiar with, which makes it easier for
users to read and edit.

Other deviations from standard JSON include allowing comments (lines starting
with ``#`` are treated as comments) and allowing the separator characters
(``:``, ``=``, ``,``) to appear in excess or abundance. That means that you can
write ``key = value`` or ``key: value`` or ``key value`` and it will be
interpreted the same way. You may also write ``[val1, val2, val3]`` or
``[val1, val2, val3, ]`` or ``[val1 val2 val3]`` and it will be interpreted
the same way. This is allowed because the SPA-JSON parser in fact ignores all
the separator characters (the real separator is the space character).

Examples of valid SPA-JSON files:

.. code-block::

    # This is the most common syntax
    section1 = {
      string-key = value1
      number-key = 123
      boolean-key = true
    }
    section2 = [
      val1, val2, val3
    ]

.. code-block::

    # Mixed syntax
    section1 {
      "string-key" = "value1"
      number-key: 123
      boolean-key true
    }
    section2 = [
      val1, val2 val3,
    ]

.. code-block::

   # Standard JSON (albeit this comment line)
   "section1": {
     "string-key": "value1",
     "number-key": 123,
     "boolean-key": true
   }
   "section2": [
     "val1", "val2", "val3"
   ]

Fragments
---------

Just like PipeWire, WirePlumber supports configuration fragments. This means
that the main configuration file can be split into multiple files, and all of
them will be loaded and merged together. This is mostly useful to allow users
to customize their configuration without having to modify the main file.

When loading the configuration file, WirePlumber will also look for
additional files in the directory that has the same name as the configuration
file suffixed with ``.d`` and will load all of them as well. For example,
loading ``wireplumber.conf`` will also load any ``.conf`` files under
``wireplumber.conf.d/``. This directory is searched in all the search paths
for configuration files (see :ref:`config_locations`) and the fragments are
loaded from *all* of them.

The fragments are loaded in alphabetical order, after the main configuration
file. When a JSON object appears in multiple files, the properties of the
objects are merged together. When a JSON array appears in multiple files, the
arrays are concatenated together. When merging objects, if specific properties
appear in many of those objects, the last one to be parsed always overwrites
previous ones, unless the value is also an object or array; if it is, then the
value is recursively merged using the same rules.

Sections
--------

WirePlumber reads the following standard sections from the configuration
file:

* *wireplumber.components*

  This section is an array that lists components that can be loaded by
  WirePlumber. For more information, see :ref:`config_components_and_profiles`.

* *wireplumber.profiles*

  This section is an object that defines profiles that can be loaded by
  WirePlumber. For more information, see :ref:`config_components_and_profiles`.

* *wireplumber.settings*

  This section is an object that defines settings that can be used to
  alter WirePlumber's behavior. For more information, see :ref:`config_settings`.

In addition, there are many sections that are specific to certain components,
mostly hardware monitors, such as *monitor.alsa.properties*,
*monitor.alsa.rules*, etc. These are documented further on, in the respective
sections of this documentation that describe the configuration options of
these components.

Finally, WirePlumber also reads the following sections, which are parsed
by libpipewire to configure the PipeWire context:

* *context.properties*

  Used to define properties to configure the PipeWire context and some modules.

* *context.spa-libs*

  Used to find SPA factory names. It maps a SPA factory name regular expression
  to a library name that should contain that factory. The object property names
  are the regular expressions, and the object property values are the actual
  library names:

  .. code-block::

    <factory-name regex> = <library-name>

  For example:

  .. code-block::

    context.spa-libs = {
      api.alsa.*      = alsa/libspa-alsa
      audio.convert.* = audioconvert/libspa-audioconvert
    }

  In this example, we instruct wireplumber to lookup any *api.alsa.** factory
  in the *libspa-alsa* library, and any *audio.convert.** factory
  in the *libspa-audioconvert* library.

  .. note::

     The default configuration file already contains a list of well-known
     factory names and their corresponding libraries. You should only
     need to add entries to this section if you are using custom SPA plugins.

* *context.modules*

  Used to load PipeWire modules. This does not affect the PipeWire daemon by any
  means. It exists simply to allow loading *libpipewire* modules inside
  WirePlumber. This is usually useful to load PipeWire protocol extensions,
  so that you can export custom objects to PipeWire and other clients.

  .. note::

     PipeWire modules can also be loaded as :ref:`components <config_components_and_profiles>`,
     which may be preferrable since it allows you to load them conditionally
     based on the profile and component dependencies.

  Each module is described by a JSON object containing the module's *name*,
  its arguments (*args*) and a combination of *flags*, which can be ``ifexists``
  and ``nofail``.

  .. code-block::

    {
      name = <module-name>
      [ args = { <key> = <value> ... } ]
      [ flags = [ [ ifexists ] [ nofail ] ]
    }

  For example:

  .. code-block::

    context.modules = [
      { name = libpipewire-module-adapter }
      {
        name = libpipewire-module-metadata,
        flags = [ ifexists ]
      }
    ]

  The above example loads both PipeWire adapter and metadata modules. The
  metadata module will be ignored if not found because of its ``ifexists`` flag.
