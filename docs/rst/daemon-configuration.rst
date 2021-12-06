 .. _daemon-configuration:

Configuration
=============

WirePlumber is a heavily modular daemon. By itself, it doesn't do anything
except load the configured modules. All the rest of the logic is implemented
inside those modules.

Modular design ensures that it is possible to swap the implementation of
specific functionality without having to re-implement the rest of it, allowing
flexibility on target-sensitive parts, such as policy management and
making use of non-standard hardware.

*wireplumber.conf*
------------------

This is WirePlumber's main configuration file. It is read at startup, before
connecting to the PipeWire daemon, and its purpose is to define the different
sections of the daemon's configuration. Since the format of this configuration
file is in JSON, all sections are essentially JSON objects. Lines starting with
*#* are treated as comments and ignored. The list of all possible section JSON
objects are:

* *context.properties*

  Used to define properties to configure the PipeWire context and some modules.

  Example::

    context.properties = {
      application.name = WirePlumber
      Log.level = 2
    }

  This sets the daemon's name to *WirePlumber* and the log level to *2*, which
  only displays errors and warnings. See the Debug_ section for more details.

  .. _Debug: https://pipewire.pages.freedesktop.org/wireplumber/daemon-logging.html

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
  a combination of 2 flags: `ifexists` flag is given, the module is ignored when
  not found; if `nofail` is given, module initialization failures are ignored::

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
  metadata module will be ignored if not found because of its `ifexists` flag.

* *context.components*

  Used to load WirePlumber components. Components can be either a WirePlumber
  module written in C that is loaded dynamically, or a directory with Lua
  configuration files::

    { name = <component-name>, type = <component-type> }

  Example::

    context.components = [
      { name = libwireplumber-module-lua-scripting, type = module }
      { name = main.lua, type = config/lua }
    ]

  This will load the WirePlumber Lua scripting module dynamically, and any Lua
  file that is placed under the *main.lua.d* directory, which can load other
  components from there (See Lua configuration directories below for more
  details).

Location of configuration files
-------------------------------

WirePlumber's default locations of its configuration files are determined at
compile time by the build system. Typically, those end up being
`XDG_CONFIG_DIR/wireplumber`, `/etc/wireplumber`, and
`/usr/share/wireplumber`, in that order of priority.

In more detail, the latter two are controlled by the `--sysconfdir` and `--datadir`
meson options. When those are set to an absolute path, such as `/etc`, the
location of the configuration files is set to be `$sysconfdir/wireplumber`.
When set to a relative path, such as `etc`, then the installation prefix (`--prefix`)
is prepended to the path: `$prefix/$sysconfdir/wireplumber`

The three locations are intended for custom user configuration,
host-specific configuration and distribution-provided configuration,
respectively. At runtime, WirePlumber will search the directories
for the highest-priority directory to contain the `wireplumber.conf`
configuration file. This allows a user or system administrator to easily
override the distribution provided configuration files by placing an equally
named file in the respective directory.

It is possible to override the lookup path at runtime by passing the
`--config-file` or `-c` option::

  wireplumber --config-file=src/config/wireplumber.conf

It is also possible to override the whole configuration directory, so that
all other configuration files are being read from a different location as well,
by setting the `WIREPLUMBER_CONFIG_DIR` environment variable::

  WIREPLUMBER_CONFIG_DIR=src/config wireplumber

If `WIREPLUMBER_CONFIG_DIR` is set, the default locations are ignored.

Location of modules
-------------------

WirePlumber modules
^^^^^^^^^^^^^^^^^^^

Like with configuration files, WirePlumber's default location of its modules is
determined at compile time by the build system. Typically, it ends up being
`/usr/lib/wireplumber-0.1` (or `/usr/lib/<arch-triplet>/wireplumber-0.1` on
multiarch systems)

In more detail, this is controlled by the `--libdir` meson option. When
this is set to an absolute path, such as `/lib`, the location of the
modules is set to be `$libdir/wireplumber-$abi_version`. When this is set
to a relative path, such as `lib`, then the installation prefix (`--prefix`)
is prepended to the path\: `$prefix/$libdir/wireplumber-$abi_version`.

It is possible to override this directory at runtime by setting the
`WIREPLUMBER_MODULE_DIR` environment variable::

  WIREPLUMBER_MODULE_DIR=build/modules wireplumber

PipeWire and SPA modules
^^^^^^^^^^^^^^^^^^^^^^^^

PipeWire and SPA modules are not loaded from the same location as WirePlumber's
modules. They are loaded from the location that PipeWire loads them.

It is also possible to override these locations by using environment variables:
`SPA_PLUGIN_DIR` and `PIPEWIRE_MODULE_DIR`. For more details, refer to
PipeWire's documentation.


Lua Configuration Directories
-----------------------------

A Lua directory can contain a list of Lua configuration files. Those files are
loaded alphabetically by filename so that user can control the order in which
Lua configuration files are executed.

The default WirePlumber configuration has the following Lua configuration
directories (note that this can change in future releases):

* *main.lua.d*

This directory contains the main WirePlumber Lua configuration files. Here you
will find the configuration for ALSA, V4L2 and libcamera monitors configuration.
In addition to this, there is also access configuration for the clients.

* *bluetooth.lua.d*

This directory is only used for Bluetooth configuration.


* *policy.lua.d*

This directory is used for both the policy and endpoints configuration.



Lua Configuration Files
-----------------------

Some of the most relevant Lua configuration files from the Lua configuration
directories are:

main.lua.d/\*-alsa-config.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This configuration file is charged to configure the ALSA nodes created by
PipeWire. Users can configure how these ALSA nodes are created by defining a
set of properties and rules:

* *alsa_monitor.properties*

  This is a simple Lua object that has key value pairs used as properties.

  Example::

    alsa_monitor.properties = {
      ["alsa.jack-device"] = false,
      ["alsa.reserve"] = true,
    }

  The above example will configure the ALSA monitor to not enable the JACK
  device, and do ALSA device reservation using the mentioned DBus interface.

  A list of valid properties are::

    ["alsa.jack-device"] = false

  Creates a JACK device if set to `true`. This is not enabled by default because it
  requires that the PipeWire JACK replacement libraries are not used by the
  session manager, in order to be able to connect to the real JACK server.::

    ["alsa.reserve"] = true

  Reserve ALSA devices via org.freedesktop.ReserveDevice1 on D-Bus.::

    ["alsa.reserve.priority"] = -20

  The used ALSA device reservation priority.::

    ["alsa.reserve.application-name"] = "WirePlumber"

  The used ALSA device reservation application name.


* *alsa_monitor.rules*

  This is a Lua array that can contain objects with rules for a device or node.
  Those objects have 2 properties. The first one is `matches`, which allow users
  to define rules to match a device or node. It is an array of properties that
  all need to match the regexp. If any of the matches work, the actions are
  executed for the object. The second property is `apply_properties`, and it is
  used to apply properties on the matched object.

  Example::

    alsa_monitor.rules = {
        matches = {
          {
            { "device.name", "matches", "alsa_card.*" },
          },
        },
        apply_properties = {
          ["api.alsa.use-acp"] = true,
        }
    }

  This sets the API ALSA use ACP property to all devices with a name that
  matches the `alsa_card.*` pattern.

  A list of valid properties are::

    ["api.alsa.use-acp"] = true

  Use the ACP (alsa card profile) code to manage the device. This will probe the
  device and configure the available profiles, ports and mixer settings. The
  code to do this is taken directly from PulseAudio and provides devices that
  look and feel exactly like the PulseAudio devices.::

    ["api.alsa.use-ucm"] = true

  By default, the UCM configuration is used when it is available for your device.
  With this option you can disable this and use the ACP profiles instead.::

    ["api.alsa.soft-mixer"] = false

  Setting this option to true will disable the hardware mixer for volume control
  and mute. All volume handling will then use software volume and mute, leaving
  the hardware mixer untouched. The hardware mixer will still be used to mute
  unused audio paths in the device.::

    ["api.alsa.ignore-dB"] = false

  Setting this option to true will ignore the decibel setting configured by the
  driver. Use this when the driver reports wrong settings.::

    ["device.profile-set"] = "profileset-name"

  This option can be used to select a custom profile set name for the device.
  Usually this is configured in Udev rules but it can also be specified here.::

    ["device.profile"] = "default profile name"

  The default active profile name.::

    ["api.acp.auto-profile"] = false

  Automatically select the best profile for the device. Normally this option is
  disabled because the session manager will manage the profile of the device.
  The session manager can save and load previously selected profiles. Enable
  this if your session manager does not handle this feature.::

    ["api.acp.auto-port"] = false

  Automatically select the highest priority port that is available. This is by
  default disabled because the session manager handles the task of selecting and
  restoring ports. It can, for example, restore previously saved volumes. Enable
  this here when the session manager does not handle port restore.

  Some of the other properties that might be configured on devices::

    ["device.nick"] = "My Device",
    ["device.description"] = "My Device"

  `device.description` will show up in most apps when a device name is shown.

main.lua.d/\*-v4l2-config.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Using the same format as the the ALSA monitor properties and rules from above.
This configuration file is charged to configure the V4L2 nodes created by
PipeWire.

Example::

  v4l2_monitor.rules = {
    matches = {
      {
        { "device.name", "matches", "v4l2_device.*" },
      },
    },
    apply_properties = {
      ["node.pause-on-idle"] = false,
    },
  }

This will set the pause node on idle all V4L2 devices whose device name matches
the `v4l2_device.*` pattern.

main.lua.d/\*-default-access-config.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Using a similar format as the ALSA monitor, this configuration file is charged
to configure the client objects created by PipeWire.

* *default_access.properties*

  A Lua object that contains generic client configuration properties in the
  for of key pairs.

  Example::

    default_access.properties = {
      ["enable-flatpak-portal"] = true,
    }

  The above example sets to `true` the `enable-flatpak-portal` property.

  The list of valid properties are::

    ["enable-flatpak-portal"] = true,

  Whether to enable the flatpak portal or not.

* *default_access.rules*

  This is a Lua array that can contain objects with rules for a client object.
  Those Lua objects have 2 properties. Similar to the ALSA configuration, the
  first property is `matches`, which allow users to define rules to match a
  client object. The second property is `default_permissions`, and it is
  used to set permissions on the matched client object.

  Example::

    {
      matches = {
        {
          { "pipewire.access", "=", "flatpak" },
        },
      },
      default_permissions = "rx",
    }

  This grants read and execute permissions to all clients that have the
  `pipewire.access` property set to `flatpak`.

  Possible permissions are any combination of `r`, `w` and `x` for read, write
  and execute; or `all` for all kind of permissions.


bluetooth.lua.d/\*-bluez-config.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Using the same format as the ALSA monitor, this configuration file is charged
to configure the Bluetooth device and nodes created by PipeWire.

* *bluez_monitor.properties*

  A Lua object that contains generic client configuration properties in the
  for of key pairs.

  Example::

    bluez_monitor.properties = {
      ["bluez5.enable-msbc"] = true,
    }

  This example will enable the MSBC codec in connected Bluetooth devices that
  support it.

  The list of valid properties are::

    ["bluez5.enable-sbc-xq"] = true

  Enables the SBC-XQ codec in connected Blueooth devices that support it::

    ["bluez5.enable-msbc"] = true

  Enables the MSBC codec in connected Blueooth devices that support it::

    ["bluez5.enable-hw-volume"] = true

  Enables hardware volume controls in Bluetooth devices that support it::

    ["bluez5.headset-roles"] = "[ hsp_hs hsp_ag hfp_hf hfp_ag ]"

  Enabled headset roles (default: [ hsp_hs hfp_ag ]), this property only applies
  to native backend. Currently some headsets (Sony WH-1000XM3) are not working
  with both hsp_ag and hfp_ag enabled, disable either hsp_ag or hfp_ag to work
  around it.

  Supported headset roles: `hsp_hs` (HSP Headset), `hsp_ag` (HSP Audio Gateway),
  `hfp_hf` (HFP Hands-Free) and `hfp_ag` (HFP Audio Gateway)::

    ["bluez5.codecs"] = "[ sbc sbc_xq aac ]"

  Enables `sbc`, `sbc_zq` and `aac` A2DP codecs.

  Supported codecs: `sbc`, `sbc_xq`, `aac`, `ldac`, `aptx`, `aptx_hd`, `aptx_ll`,
  `aptx_ll_duplex`, `faststream`, `faststream_duplex`.

  All codecs are supported by default::

    ["bluez5.hfphsp-backend"] = "native"

  HFP/HSP backend (default: native). Available values: `any`, `none`, `hsphfpd`,
  `ofono` or `native`.::

    ["bluez5.default.rate"] = 48000

  The bluetooth default audio rate.::

    ["bluez5.default.channels"] = 2

  The bluetooth default number of channels.

* *bluez_monitor.rules*

  Like the ALSA configuration, this is a Lua array that can contain objects with
  rules for a Bluetooth device or node. Those objects have 2 properties. The
  first one is `matches`, which allow users to define rules to match a Bluetooth
  device or node. The second property is `apply_properties`, and it is used to
  apply properties on the matched Bluetooth device or node.

  Example::

    {
      matches = {
        {
          { "device.name", "matches", "bluez_card.*" },
        },
      },
      apply_properties = {
         ["bluez5.auto-connect"]  = "[ hfp_hf hsp_hs a2dp_sink ]"
      }
    }

  This will set the auto-connect property to `hfp_hf`, `hsp_hs` and `a2dp_sink`
  on bluetooth devices whose name matches the `bluez_card.*` pattern.

  A list of valid properties are::

    ["bluez5.auto-connect"] = "[ hfp_hf hsp_hs a2dp_sink ]"

  Auto-connect device profiles on start up or when only partial profiles have
  connected. Disabled by default if the property is not specified.

  Supported values are: `hfp_hf`, `hsp_hs`, `a2dp_sink`, `hfp_ag`, `hsp_ag` and
  `a2dp_source`.::

    ["bluez5.hw-volume"] = "[ hfp_ag hsp_ag a2dp_source ]"

  Hardware volume controls (default: `hfp_ag`, `hsp_ag`, and `a2dp_source`)

  Supported values are: `hfp_hf`, `hsp_hs`, `a2dp_sink`, `hfp_ag`, `hsp_ag` and
  `a2dp_source`.::

    ["bluez5.a2dp.ldac.quality"] = "auto"

  LDAC encoding quality.

  Available values: `auto` (Adaptive Bitrate, default),
  `hq` (High Quality, 990/909kbps), `sq` (Standard Quality, 660/606kbps) and
  `mq` (Mobile use Quality, 330/303kbps).::

    ["bluez5.a2dp.aac.bitratemode"] = 0

  AAC variable bitrate mode.

  Available values: 0 (cbr, default), 1-5 (quality level).::

    ["device.profile"] = "a2dp-sink"

  Profile connected first.

  Available values: `a2dp-sink` (default) or `headset-head-unit`.

policy.lua.d/\*-default-policy.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This file contains Generic default policy properties that can be configured.

* *default_policy.policy*

  This is a Lua object that contains several properties that change the
  behavior of the default WirePlumber policy.

  Example::

    default_policy.policy = {
      ["move"] = true,
    }

  The above example will set the `move` policy property to `true`.

  The list of supported properties are::

    ["move"] = true

  Moves session items when metadata `target.node` changes.::

    ["follow"] = true

  Moves session items to the default device when it has changed.::

    ["audio.no-dsp"] = false

  Set to `true` to disable channel splitting & merging on nodes and enable
  passthrough of audio in the same format as the format of the device. Note that
  this breaks JACK support; it is generally not recommended.::

    ["duck.level"] = 0.3

  How much to lower the volume of lower priority streams when ducking. Note that
  this is a linear volume modifier (not cubic as in PulseAudio).

policy.lua.d/\*-endpoints-config.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Endpoints are nodes that can group multiple clients into different groups or
  roles. This is useful if a user wants to apply specific actions when a client
  is connected to a particular role/endpoint. This configuration file allows
  users to configure those endpoints and their actions.

* *default_policy.policy.roles*

  This is a Lua array with objects defining the actions of each role.

  Example::

    default_policy.policy.roles = {
      ["Multimedia"] = {
        ["alias"] = { "Movie", "Music", "Game" },
        ["priority"] = 10,
        ["action.default"] = "mix",
      }
      ["Notification"] = {
        ["priority"] = 20,
        ["action.default"] = "duck",
        ["action.Notification"] = "mix",
      }
    }

  The above example defines actions for both `Multimedia` and `Notification`
  roles. Since the Notification role has more priority than the Multimedia
  role, when a client connects to the Notification endpoint, it will `duck`
  the volume of all Multimedia clients. If Multiple Notification clients want
  to play audio, only the Notifications audio will be mixed.

  Possible values of actions are: `mix` (Mixes audio),
  `duck` (Mixes and lowers the audio volume) or `cork` (Pauses audio).

* *default_policy.policy.endpoints*

  This is a Lua array with objects defining the endpoints that the user wants
  to create.

  Example::

    default_policy.endpoints = {
      ["endpoint.multimedia"] = {
        ["media.class"] = "Audio/Sink",
        ["role"] = "Multimedia",
      }
    },
    ["endpoint.notifications"] = {
      ["media.class"] = "Audio/Sink",
      ["role"] = "Notification",
    }

  This example creates 2 endpoints, with names `endpoint.multimedia` and
  `endpoint.notifications`; and assigned roles `Multimedia` and `Notification`
  respectively. Both endpoints have `Audio/Sink` media class, and so are only
  used for playback.
