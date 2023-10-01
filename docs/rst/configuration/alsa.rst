.. _config_alsa:

ALSA Configuration
==================

ALSA devices are created and managed by the session manager with the *alsa.lua*
monitor script. ``wireplumber.conf.d/alsa.conf`` contains :ref:`default
configuration options<manipulate_configuration_options>`  which control the alsa monitor.

Simple Configuration Options
----------------------------

All the :ref:`simple configuration options<configuration_option_types>` can be
:ref:`overridden<manipulate_configuration_options>` or can be changed
:ref:`live<live_configuration_options>`. They are commented in the default
location, as they are built into WirePlumber. Below is the explanation of each
of these simple configuration options.

.. code-block::

  monitor.alsa.midi = true

Enables MIDI functionality

.. code-block::

  monitor.alsa.midi.monitoring = true

Enables monitoring of alsa MIDI devices

.. code-block::

  monitor.alsa.jack-device = true

Creates a JACK device if set to ``true``. This is not enabled by default because
it requires that the PipeWire JACK replacement libraries are not used by the
session manager, in order to be able to connect to the real JACK server.

.. code-block::

  monitor.alsa.reserve = true

Reserve ALSA devices via *org.freedesktop.ReserveDevice1* on D-Bus.

.. code-block::

  monitor.alsa.reserve-priority = -20

The used ALSA device reservation priority. constructing the the MIDI bridge node
properties can go here.

.. code-block::

  monitor.alsa.reserve-application-name = WirePlumber

The used ALSA device reservation application name.


Complex Configuration Options
-----------------------------

The :ref:`complex configuration options<configuration_option_types>`  can be either
:ref:`overridden<manipulate_configuration_options>`  or :ref:`extended<manipulate_configuration_options>` but
they cannot be changed :ref:`live<live_configuration_options>`. Below is the explanation of each
of these complex configuration options.

.. code-block::

  monitor.alsa.properties = {
  }

The properties used when constructing the 'api.alsa.enum.udev' plugin can go
into this section.

.. code-block::

  monitor.alsa.midi.node-properties = {
    node.name = "Midi-Bridge" api.alsa.disable-longname = true
  }

The properties used when constructing the the MIDI bridge node properties can go
into this section, the two properties set above are self explanatory.

Device settings
^^^^^^^^^^^^^^^

PipeWire devices are ALSA sound cards.

.. code-block::

  api.alsa.use-acp = true

Use the ACP (alsa card profile) code to manage the device. This will probe the
device and configure the available profiles, ports and mixer settings. The code
to do this is taken directly from PulseAudio and provides devices that look and
feel exactly like the PulseAudio devices.

.. code-block::

  api.alsa.use-ucm = true

By default, the UCM configuration is used when it is available for your device.
With this option you can disable this and use the ACP profiles instead.

.. code-block::

  api.alsa.soft-mixer = false

Setting this option to true will disable the hardware mixer for volume control
and mute. All volume handling will then use software volume and mute, leaving
the hardware mixer untouched. The hardware mixer will still be used to mute
unused audio paths in the device.

.. code-block::

  api.alsa.ignore-dB = false

Setting this option to true will ignore the decibel setting configured by the
driver. Use this when the driver reports wrong settings.

.. code-block::

  device.profile-set = "profileset-name"

This option can be used to select a custom profile set name for the device.
Usually this is configured in Udev rules but it can also be specified here.

.. code-block::

  device.profile = "default profile name"

The default active profile name.

.. code-block::

  api.acp.auto-profile = false

Automatically select the best profile for the device. Normally this option is
disabled because the session manager will manage the profile of the device. The
session manager can save and load previously selected profiles. Enable this if
your session manager does not handle this feature.

.. code-block::

  api.acp.auto-port = false

Automatically select the highest priority port that is available. This is by
default disabled because the session manager handles the task of selecting and
restoring ports. It can, for example, restore previously saved volumes. Enable
this here when the session manager does not handle port restore.

.. code-block:: lua

  ["api.acp.probe-rate"] = 48000

Sets the samplerate used for probing the ALSA devices and collecting the
profiles and ports.

.. code-block:: lua

  ["api.acp.pro-channels"] = 64

Sets the number of channels to use when probing the Pro Audio profile. Normally,
the maximum amount of channels will be used but with this setting this can be
reduced, which can make it possible to use other samplerates on some devices.

Some of the other settings that might be configured on devices:

.. code-block::

  device.nick = "My Device", device.description = "My Device"

``device.description`` will show up in most apps when a device name is shown.

Node Settings
^^^^^^^^^^^^^

Nodes are sinks or sources on a ALSA sound card. In addition to the generic
stream node configuration options, there are some alsa specific options as well:

.. code-block::

    priority.driver = 2000

This configures the node driver priority. Nodes with higher priority will be
used as a driver in the graph. Other nodes with lower priority will have to
resample to the driver node when they are joined in the same graph. The default
value is set based on some heuristics.

.. code-block::

    priority.session = 1200

This configures the priority of the node when selecting a default node. Higher
priority nodes will be more likely candidates as a default node.

.. note::

  By default, sources have a ``priority.session`` value around 1600-2000 and
  sinks have a value around 600-1000. If you are increasing the priority of a
  sink, it is **not advised** to use a value higher than 1500, as it may cause a
  sink's monitor to be selected as a default source.

.. code-block::

    node.pause-on-idle = false

Pause-on-idle will stop the node when nothing is linked to it anymore. This is
by default false because some devices cause a pop when they are opened/closed.
The node will, normally, pause and suspend after a timeout (see
suspend-node.lua).

.. code-block::

    session.suspend-timeout-seconds = 5  -- 0 disables suspend

This option configures a different suspend timeout on the node. By default this
is 5 seconds. For some devices (HiFi amplifiers, for example) it might make
sense to set a higher timeout because they might require some time to restart
after being idle.

A value of 0 disables suspend for a node and will leave the ALSA device busy.
The device can then manually be suspended with ``pactl suspend-sink|source``.

**The following properties can be used to configure the format used by the ALSA
device:**

.. code-block::

    audio.format = "S16LE"

By default, PipeWire will use a 32 bits sample format but a different format can
be set here.

The Audio rate of a device can be set here:

.. code-block::

    audio.rate = 44100

By default, the ALSA device will be configured with the same samplerate as the
global graph. If this is not supported, or a custom values is set here,
resampling will be used to match the graph rate.

.. code-block::

    audio.channels = 2 audio.position = "FL,FR"

By default the channels and their position are determined by the selected Device
profile. You can override this setting here and optionally swap or reconfigure
the channel positions.

.. code-block::

    api.alsa.use-chmap = false

Use the channel map as reported by the driver. This is disabled by default
because it is often wrong and the ACP code handles this better.

.. code-block::

    api.alsa.disable-mmap  = true

PipeWire will by default access the memory of the device using mmap. This can be
disabled and force the usage of the slower read and write access modes in case
the mmap support of the device is not working properly.

.. code-block::

    channelmix.normalize = true

Makes sure that during such mixing & resampling original 0 dB level is
preserved, so nothing sounds wildly quieter/louder.

.. code-block::

    channelmix.mix-lfe = true

Creates "center" channel for X.0 recordings from front stereo on X.1 setups and
pushes some low-frequency/bass from "center" from X.1 recordings into front
stereo on X.0 setups.

.. code-block::

    monitor.channel-volumes = false

By default, the volume of the sink/source does not influence the volume on the
monitor ports. Set this option to true to change this. PulseAudio has
inconsistent behaviour regarding this option, it applies channel-volumes only
when the sink/source is using software volumes.

ALSA buffer properties
^^^^^^^^^^^^^^^^^^^^^^

PipeWire uses a timer to consume and produce samples to/from ALSA devices. After
every timeout, it queries the device hardware pointers of the device and uses
this information to set a new timeout. See also this example program.

By default, PipeWire handles ALSA batch devices differently from non-batch
devices. Batch devices only get their hardware pointers updated after each
hardware interrupt. Non-batch devices get updates independent of the interrupt.
This means that for batch devices we need to set the interrupt at a sufficiently
high frequency (at the cost of CPU usage) while for non-batch devices we want to
set the interrupt frequency as low as possible (to save CPU).

For batch devices we also need to take the extra buffering into account caused
by the delayed updates of the hardware pointers.

Most USB devices are batch devices and will be handled as such by PipeWire by
default.

There are 2 tunable parameters to control the buffering and timeouts in a device

.. code-block::

    api.alsa.period-size = 1024

This sets the device interrupt to every period-size samples for non-batch
devices and to half of this for batch devices. For batch devices, the other half
of the period-size is used as extra buffering to compensate for the delayed
update. So, for batch devices, there is an additional period-size/2 delay. It
makes sense to lower the period-size for batch devices to reduce this delay.

.. code-block::

    api.alsa.headroom = 0

This adds extra delay between the hardware pointers and software pointers. In
most cases this can be set to 0. For very bad devices or emulated devices (like
in a VM) it might be necessary to increase the headroom value. In summary, this
is the overview of buffering and timings:


  ============== ========================================== =========
  Property       Batch                                      Non-Batch
  ============== ========================================== =========
  IRQ Frequency  api.alsa.period-size/2                     api.alsa.period-size
  Extra Delay    api.alsa.headroom + api.alsa.period-size/2 api.alsa.headroom
  ============== ========================================== =========

It is possible to disable the batch device tweaks with:

.. code-block::

    api.alsa.disable-batch = true

It removes the extra delay added of period-size/2 if the device can support
this. For batch devices it is also a good idea to lower the period-size (and
increase the IRQ frequency) to get smaller batch updates and lower latency.

ALSA extra latency properties
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Extra internal delay in the DAC and ADC converters of the device itself can be
set with the ``latency.internal.*`` properties:

.. code-block::

    latency.internal.rate = 256 latency.internal.ns = 0

You can configure a latency in samples (relative to rate with
``latency.internal.rate``) or in nanoseconds (``latency.internal.ns``). This
value will be added to the total reported latency by the node of the device.

You can use a tool like ``jack_iodelay`` to get the number of samples of
internal latency of your device.

This property is also adjustable at runtime with the ``ProcessLatency`` param.
You will need to find the id of the Node you want to change. For example: Query
the current internal latency of an ALSA node with id 58:

.. code-block:: console

    $ pw-cli e 58 ProcessLatency Object: size 80, type
    Spa:Pod:Object:Param:ProcessLatency (262156), id
    Spa:Enum:ParamId:ProcessLatency (16)
      Prop: key Spa:Pod:Object:Param:ProcessLatency:quantum (1), flags 00000000
        Float 0.000000
      Prop: key Spa:Pod:Object:Param:ProcessLatency:rate (2), flags 00000000
        Int 0
      Prop: key Spa:Pod:Object:Param:ProcessLatency:ns (3), flags 00000000
        Long 0

Set the internal latency to 256 samples:

.. code-block:: console

    $ pw-cli s 58 ProcessLatency '{ rate = 256 }' Object: size 32, type
    Spa:Pod:Object:Param:ProcessLatency (262156), id
    Spa:Enum:ParamId:ProcessLatency (16)
      Prop: key Spa:Pod:Object:Param:ProcessLatency:rate (2), flags 00000000
        Int 256
    remote 0 node 58 changed remote 0 port 70 changed remote 0 port 72 changed
    remote 0 port 74 changed remote 0 port 76 changed

Startup tweaks
^^^^^^^^^^^^^^

Some devices need some time before they can report accurate hardware pointer
positions. In those cases, an extra start delay can be added that is used to
compensate for this startup delay:

.. code-block::

    api.alsa.start-delay = 0

It is unsure when this tunable should be used.

IEC958 (S/PDIF) passthrough
^^^^^^^^^^^^^^^^^^^^^^^^^^^

S/PDIF passthrough will only be enabled when the accepted codecs are configured
on the ALSA device.

This can be done in 3 different ways:

  1. Use pavucontrol and toggle the codecs in the output advanced section.

  2. Modify the ``["iec958.codecs"] = "[ PCM DTS AC3 MPEG MPEG2-AAC EAC3 TrueHD
     DTS-HD ]"`` node property to something.

  3. Use ``pw-cli s <node-id> Props '{ iec958Codecs : [ PCM ] }'`` to modify the
     codecs at runtime.

Examples
^^^^^^^^

The below examples contain rules configuring properties on both devices and
device nodes.

  .. code-block::

    monitor.alsa.rules = [
      {
        matches = [
          {
            # This matches the needed sound card. device.name =
            "<sound_card_name>"
          }
        ] actions = {
          update-props = {
            # Apply all the desired device settings here. api.alsa.use-acp =
            true
          }
        }
      }
      {
        matches = [
          {
            # "~" triggers wild card evaluation, only "*" is supported.
            device.name = "~my-sound-card*" device.product.name = "~Tiger*"
          }
        ] actions = {
          update-props = {
            # Apply all the desired device settings here. device.nick =
            "my-card"
          }
        }
      }
      {
        matches = [
          {
            # This matches all the input device nodes. # "~" triggers wild card
            evaluation, only "*" is supported. node.name = "~alsa_input.*"
          }
          {
            # This matches all the output device nodes. node.name =
            "~alsa_output.*"
          } # either input or output nodes
        ] actions = {
          update-props = {
            # Apply all the desired node settings here. node.nick              =
            "My Node" node.description       = "My Node Description"
            api.alsa.period-size   = 1024 api.alsa.period-num    = 2
            api.alsa.headroom      = 0

          }
        }
      }
      {
        matches = [
          {
            # "~" triggers wild card evaluation, only "*" is supported.
            node.name = "~libcamera*" device.api = "libcamera"
          } # all the conditions should be met with in the curly braces for the
          # match to evaluate to true
        ] actions = {
          update-props = {
            # Apply all the desired node settings here. node.nick = "my-libcam"
          }
        }
      }
    ]

.. note::

    Device and Node settings both go into monitor.alsa.rules JSON section and
    they are also called rule based configuration options in that the device or
    node will have to be filtered first using the match rules. Settings can be
    set either on all the devices/nodes or on specific devices/nodes, depending
    on how the match rules are setup.

.. note::

    The properties set in the update-props section, can be PipeWire properties
    which trigger some action or they can be new properties that the devices or
    nodes will be created with. These new properties can be read or written from
    scripts or modules. After the creation of the devices and nodes new
    properties cannot be created on them.
