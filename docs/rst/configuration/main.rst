.. _config_main:

Main configuration file
=======================

The main configuration file is by default called ``wireplumber.conf``. This can
be changed on the command line by passing the ``--config-file`` or ``-c`` option::

  wireplumber --config-file=bluetooth.conf

The ``--config-file`` option is useful to run multiple instances of wireplumber
that do separate tasks each. For more information on this subject, see the
:ref:`Multiple Instances <config_multi_instance>` section.

The format of this configuration file is the variant of JSON that is also
used in PipeWire configuration files. Note that this is subject to change
in the future.

All sections are essentially JSON objects. Lines starting with *#* are treated
as comments and ignored. The list of all possible section JSON objects are:

* *context.properties*

  Used to define properties to configure the PipeWire context and some modules.

  Example::

    context.properties = {
      application.name = WirePlumber
      log.level = 2
    }

  This sets the daemon's name to *WirePlumber* and the log level to *2*, which
  only displays errors and warnings. See the
  :ref:`Debug Logging <logging>` section for more details.

* *context.spa-libs*

  Used to find spa factory names. It maps a spa factory name regular expression
  to a library name that should contain that factory. The object property names
  are the regular expression, and the object property values are the actual
  library name::

    <factory-name regex> = <library-name>

  Example::

    context.spa-libs = {
      api.alsa.*      = alsa/libspa-alsa
      audio.convert.* = audioconvert/libspa-audioconvert
    }

  In this example, we instruct wireplumber to only any *api.alsa.** factory name
  from the *libspa-alsa* library, and also any *audio.convert.** factory name
  from the *libspa-audioconvert* library.

* *context.modules*

  Used to load PipeWire modules. This does not affect the PipeWire daemon by any
  means. It exists simply to allow loading *libpipewire* modules in the PipeWire
  core that runs inside WirePlumber. This is usually useful to load PipeWire
  protocol extensions, so that you can export custom objects to PipeWire and
  other clients.

  Users can also pass key-value pairs if the specific module has arguments, and
  a combination of 2 flags: ``ifexists`` flag is given, the module is ignored when
  not found; if ``nofail`` is given, module initialization failures are ignored::

    {
      name = <module-name>
      [ args = { <key> = <value> ... } ]
      [ flags = [ [ ifexists ] [ nofail ] ]
    }

  Example::

    context.modules = [
      { name = libpipewire-module-adapter }
      {
        name = libpipewire-module-metadata,
        flags = [ ifexists ]
      }
    ]

  The above example loads both PipeWire adapter and metadata modules. The
  metadata module will be ignored if not found because of its ``ifexists`` flag.

* *context.components*

  Used to load WirePlumber components. Components can be either WirePlumber
  modules written in C, WirePlumber scripts or other configuration
  files::

    { name = <component-name>, type = <component-type> }

  Valid component types include:

  * ``module``: A WirePlumber shared object module
  * ``script/lua``: A WirePlumber Lua script
    (requires ``libwireplumber-module-lua-scripting``)
  * ``config/lua``: A WirePlumber Lua configuration file
    (requires ``libwireplumber-module-lua-scripting``)

  Example::

    context.components = [
      { name = libwireplumber-module-lua-scripting, type = module }
      { name = main.lua, type = config/lua }
    ]

  This will load the WirePlumber lua-scripting module, dynamically, and then
  it will also load any components specified in the ``main.lua`` file.

  .. note::

    When loading lua configuration files, WirePlumber will also look for
    additional files in the directory suffixed with ``.d`` and will load
    all of them as well. For example, loading ``example.lua`` will also load
    any ``.lua`` files under ``example.lua.d/``. In addition, the presence of the
    main file is optional, so it is valid to specify ``example.lua`` in the
    component name, while ``example.lua`` doesn't exist, but ``example.lua.d/``
    exists instead and has ``.lua`` files to load.

    For more information about lua configuration files, see the
    :ref:`Lua configuration files <config_lua>` section.

