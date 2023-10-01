.. _config_main:

Main configuration file
=======================

The main configuration file is by default ``wireplumber.conf``. This can
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

Common configs are present in the main configuration file(wireplumber.conf),
rest of the configs that can be grouped logically are grouped into separate
files and are placed under ``wireplumber.conf.d/``. More on this below.

.. note::

  The config search folder depends on the main config file. If the config file
  is bluetooth.conf then WirePlumber will search for ``bluetooth.conf.d`` not in
  the ``wireplumber.conf.d/`` folder.

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

* *wireplumber.components*

  Used to load WirePlumber components. Components can be either WirePlumber
  modules written in C or WirePlumber scripts written in Lua.

  Syntax::

    { name = <component-name>, type = <component-type>, deps = <dependent-setting>, flags = <flags> }

  * type:

  Valid component types include:

    * ``module``: A WirePlumber shared object module
    * ``script/lua``: A WirePlumber Lua script
      (all Lua Scripts implicitly requires libwireplumber-module-lua-scripting module)

  Example::

    wireplumber.components = [
      { name = libwireplumber-module-lua-scripting, type = module }
      { name = monitors/alsa.lua, type = script/lua }
    ]

  * deps: components can be loaded with a dependency on a wireplumber setting.
  * flags: ifexists & nofail flags are supported in this section as well.


    * `ifexists` - signals wireplumber to ignore if the module is not found.
    * `nofail` - signals wireplumber to ignore module initialization failures.

  More Examples::

    wireplumber.components = [
      # Load `libwireplumber-module-si-node` which is of type `module`.
      { name = libwireplumber-module-si-node , type = module }

      # Load `libwireplumber-module-reserve-device` module, only if the setting `alsa_monitor.alsa.reserve` is defined as true.
      { name = libwireplumber-module-reserve-device , type = module, deps = alsa_monitor.alsa.reserve }

      # Load `alsa.lua` which is of type `script/lua`.
      { name = monitors/alsa.lua, type = script/lua }

      # Load `alsa-midi.lua` Lua Script only if `alsa_monitor.alsa.midi` setting is defined as true.
      { name = monitors/alsa-midi.lua, type = script/lua, deps = alsa_monitor.alsa.midi }

      # Load `libwireplumber-module-logind` module if the setting `bluez-enable-logind` is true.
      { name = libwireplumber-module-logind , type = module, deps = bluez-enable-logind, flags = [ ifexists ] }
    ]

  .. note::

      - `name` & `type` keys are mandatory, while `deps` and `flags` keys are optional
      - All the components are loaded during the bootup and failure in finding them or any error during the loading process is a fatal error and WirePlumber will exit.


* *wireplumber.settings*

  All the Wireplumber configurations are now grouped under this section. They
  are moved away from Lua. All the default settings are distributed into
  different files under ``wireplumber.conf.d\``. There are two :ref:`different
  types<config_types>` of config.

  * *wireplumber.virtuals*

    Virtual session items are a way of grouping different kinds of clients or
    applications(for example Music, Voice, Navigation, Gaming etc).
    The actual grouping is done based on the `media.role` of the client
    stream node.

    Virtual session items allows for that actions to be taken up at group level
    rather than at individual stream level, which can be cumbersome.

    For example imagine the following scenarios.
      * Incoming Navigation message needs to duck the volume of
        Audio playback(all the apps playing audio).
      * Incoming voice/voip call needs to stop(cork) the Audio playback.

    Virtual session items realize this functionality with ease.

    * *Defining Virtual session items*

      Example::

        virtual-items = {
          virtual-item.capture = {
            media.class = "Audio/Source"
            role = "Capture"
          }
          virtual-item.multimedia = {
            media.class = "Audio/Sink"
            role = "Multimedia"
          }
          virtual-item.navigation = {
            media.class = "Audio/Sink"
            role = "Navigation"
          }

      This example creates 3 virtual session items, with names
      ``virtual-item.capture``, ``virtual-item.multimedia`` and
      ``virtual-item.navigation`` and assigned roles ``Capture``, ``Multimedia``
      and ``Navigation`` respectively.

      First virtual item has a media class of ``Audio/Source`` used for capture
      and rest of the virtual items have ``Audio/Sink`` media class, and so are
      only used for playback.

    * *Virtual session items config*

      Example::

        Capture = {
          alias = [ "Multimedia", "Music", "Voice", "Capture" ]
          priority = 25
          action.default = "cork"
          action.capture = "mix"
          media.class = "Audio/Source"
        }
        Multimedia = {
          alias = [ "Movie" "Music" "Game" ]
          priority = 25
          action.default = "cork"
        }
        Navigation = {
          priority = 50
          action.default = "duck"
          action.Navigation = "mix"
        }


      The above example defines actions for both ``Multimedia`` and ``Navigation``
      roles. Since the Navigation role has more priority than the Multimedia
      role, when a client connects to the Navigation virtual session item, it
      will ``duck`` the volume of all Multimedia clients. If Multiple Navigation
      clients want to play audio, their audio will be mixed.

      Possible values of actions are: ``mix`` (Mixes audio),
      ``duck`` (Mixes and lowers the audio volume) or ``cork`` (Pauses audio).

    Virtual session items are not used for desktop use cases, it is more suitable
    for embedded use cases.

* *Split Configuration files*

The Main configuration file is split into multiple files. When loading the main
JSON configuration file, WirePlumber will also look for additional files in the
same directory suffixed with ``.d`` and will load all of them as well. For
example, loading ``wireplumber.conf`` will also load any files under
``wireplumber.conf.d/``. It will load all the JSON config files there. All the
configurations are logically split into files and placed in this directory.
