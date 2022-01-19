.. _config_alsa:

ALSA configuration
==================

Modifying the default configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ALSA devices are created and managed by the session manager with the *alsa.lua*
monitor script. In the default configuration, this script is loaded by
``main.lua.d/30-alsa-monitor.lua``, which also specifies an ``alsa_monitor``
global table that can be filled in with properties and rules in subsequent
config files. By default, these are filled in ``main.lua.d/50-alsa-config.lua``.

The ``alsa_monitor`` global table has 2 sub-tables:

* *alsa_monitor.properties*

  This is a simple Lua table that has key value pairs used as properties.

  Example:

  .. code-block:: lua

    alsa_monitor.properties = {
      ["alsa.jack-device"] = false,
      ["alsa.reserve"] = true,
    }

  The above example will configure the ALSA monitor to not enable the JACK
  device, and do ALSA device reservation using the mentioned DBus interface.

  A list of valid properties are:

  .. code-block:: lua

    ["alsa.jack-device"] = false

  Creates a JACK device if set to ``true``. This is not enabled by default because
  it requires that the PipeWire JACK replacement libraries are not used by the
  session manager, in order to be able to connect to the real JACK server.

  .. code-block:: lua

    ["alsa.reserve"] = true

  Reserve ALSA devices via *org.freedesktop.ReserveDevice1* on D-Bus.

  .. code-block:: lua

    ["alsa.reserve.priority"] = -20

  The used ALSA device reservation priority.

  .. code-block:: lua

    ["alsa.reserve.application-name"] = "WirePlumber"

  The used ALSA device reservation application name.


* *alsa_monitor.rules*

  This is a Lua array that can contain objects with rules for a device or node.
  Those objects have 2 properties. The first one is ``matches``, which allow
  users to define rules to match a device or node. The second property is
  ``apply_properties``, and it is used to apply properties on the matched object.

  Example:

  .. code-block:: lua

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
  matches the ``alsa_card.*`` pattern.

  The ``matches`` section is an array of arrays. On the first level, the rules
  are ORed together, so any rule match is going to apply the properties. On
  the second level, the rules are merged with AND, so they must all match.

  Example:

  .. code-block:: lua

    matches = {
      {
        { "node.name", "matches", "alsa_input.*" },
        { "alsa.driver_name", "equals", "snd_hda_intel" },
      },
      {
        { "node.name", "matches", "alsa_output.*" },
      },
    },

  This is equivalent to the following logic, in pseudocode:

  .. code-block::

    if ("node.name" MATCHES "alsa_input.*" AND "alsa.driver_name" EQUALS "snd_hda_intel" )
       OR
       ("node.name" MATCHES "alsa_output.*")
    then
       ... apply the properties ...
    end

  As you can notice, the individual rules are themselves also lua arrays. The
  first element is a property name (ex "node.name"), the second element is a
  verb and the third element is an expected value, which depends on the verb.
  Internally, this uses the ``Constraint`` API, which is documented in the
  :ref:`Object Interet API <lua_object_interest_api>` section. All the verbs
  that you can use on ``Constraint`` are also allowed here.

  .. note::

    When using the "matches" verb, the values are not complete regular expressions.
    They are wildcard patterns, which means that '*' matches an arbitrary,
    possibly empty, string and '?' matches an arbitrary character.

  All the possible properties that you can apply to devices and nodes of the
  ALSA monitor are described in the sections below.

Device properties
^^^^^^^^^^^^^^^^^

PipeWire devices correspond to the ALSA cards.
The following properties can be configured on devices created by the monitor:

.. code-block:: lua

  ["api.alsa.use-acp"] = true

Use the ACP (alsa card profile) code to manage the device. This will probe the
device and configure the available profiles, ports and mixer settings. The
code to do this is taken directly from PulseAudio and provides devices that
look and feel exactly like the PulseAudio devices.

.. code-block:: lua

  ["api.alsa.use-ucm"] = true

By default, the UCM configuration is used when it is available for your device.
With this option you can disable this and use the ACP profiles instead.

.. code-block:: lua

  ["api.alsa.soft-mixer"] = false

Setting this option to true will disable the hardware mixer for volume control
and mute. All volume handling will then use software volume and mute, leaving
the hardware mixer untouched. The hardware mixer will still be used to mute
unused audio paths in the device.

.. code-block:: lua

  ["api.alsa.ignore-dB"] = false

Setting this option to true will ignore the decibel setting configured by the
driver. Use this when the driver reports wrong settings.

.. code-block:: lua

  ["device.profile-set"] = "profileset-name"

This option can be used to select a custom profile set name for the device.
Usually this is configured in Udev rules but it can also be specified here.

.. code-block:: lua

  ["device.profile"] = "default profile name"

The default active profile name.

.. code-block:: lua

  ["api.acp.auto-profile"] = false

Automatically select the best profile for the device. Normally this option is
disabled because the session manager will manage the profile of the device.
The session manager can save and load previously selected profiles. Enable
this if your session manager does not handle this feature.

.. code-block:: lua

  ["api.acp.auto-port"] = false

Automatically select the highest priority port that is available. This is by
default disabled because the session manager handles the task of selecting and
restoring ports. It can, for example, restore previously saved volumes. Enable
this here when the session manager does not handle port restore.

Some of the other properties that might be configured on devices:

.. code-block:: lua

  ["device.nick"] = "My Device",
  ["device.description"] = "My Device"

``device.description`` will show up in most apps when a device name is shown.

Node Properties
^^^^^^^^^^^^^^^

Nodes are sinks or sources in the PipeWire graph. They correspond to the ALSA
devices. In addition to the generic stream node configuration options, there are
some alsa specific options as well:

.. code-block:: lua

    ["priority.driver"] = 2000

This configures the node driver priority. Nodes with higher priority will be
used as a driver in the graph. Other nodes with lower priority will have to
resample to the driver node when they are joined in the same graph. The default
value is set based on some heuristics.

.. code-block:: lua

    ["priority.session"] = 1500

This configures the priority of the node when selecting a default node.
Higher priority nodes will be more likely candidates as a default node.

.. note::

  By default, sources have a ``priority.session`` value around 2000 and sinks
  have a value around 1000. If you are increasing the priority of a sink, it
  is **not advised** to use a value higher than 1900, as it may cause a sink's
  monitor to be selected as a default source.

.. code-block:: lua

    ["node.pause-on-idle"] = false

Pause-on-idle will stop the node when nothing is linked to it anymore.
This is by default false because some devices cause a pop when they are
opened/closed. The node will, normally, pause and suspend after a timeout
(see suspend-node.lua).

.. code-block:: lua

    ["session.suspend-timeout-seconds"] = 5  -- 0 disables suspend

This option configures a different suspend timeout on the node.
By default this is 5 seconds. For some devices (HiFi amplifiers, for example)
it might make sense to set a higher timeout because they might require some
time to restart after being idle.

A value of 0 disables suspend for a node and will leave the ALSA device busy.
The device can then manually be suspended with ``pactl suspend-sink|source``.

**The following properties can be used to configure the format used by the
ALSA device:**

.. code-block:: lua

    ["audio.format"] = "S16LE"

By default, PipeWire will use a 32 bits sample format but a different format
can be set here.

The Audio rate of a device can be set here:

.. code-block:: lua

    ["audio.rate"] = 44100

By default, the ALSA device will be configured with the same samplerate as the
global graph. If this is not supported, or a custom values is set here,
resampling will be used to match the graph rate.

.. code-block:: lua

    ["audio.channels"] = 2
    ["audio.position"] = "FL,FR"

By default the channels and their position are determined by the selected
Device profile. You can override this setting here and optionally swap or
reconfigure the channel positions.

.. code-block:: lua

    ["api.alsa.use-chmap"] = false

Use the channel map as reported by the driver. This is disabled by default
because it is often wrong and the ACP code handles this better.

.. code-block:: lua

    ["api.alsa.disable-mmap"]  = true

PipeWire will by default access the memory of the device using mmap.
This can be disabled and force the usage of the slower read and write access
modes in case the mmap support of the device is not working properly.

.. code-block:: lua

    ["channelmix.normalize"] = true

Makes sure that during such mixing & resampling original 0 dB level is
preserved, so nothing sounds wildly quieter/louder.

.. code-block:: lua

    ["channelmix.mix-lfe"] = true

Creates "center" channel for X.0 recordings from front stereo on X.1 setups and
pushes some low-frequency/bass from "center" from X.1 recordings into front
stereo on X.0 setups.

.. code-block:: lua

    ["monitor.channel-volumes"] = false

By default, the volume of the sink/source does not influence the volume on the
monitor ports. Set this option to true to change this. PulseAudio has
inconsistent behaviour regarding this option, it applies channel-volumes only
when the sink/source is using software volumes.

ALSA buffer properties
^^^^^^^^^^^^^^^^^^^^^^

PipeWire uses a timer to consume and produce samples to/from ALSA devices.
After every timeout, it queries the device hardware pointers of the device and
uses this information to set a new timeout. See also this example program.

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

There are 2 tunable parameters to control the buffering and timeouts in a
device

.. code-block:: lua

    ["api.alsa.period-size"] = 1024

This sets the device interrupt to every period-size samples for non-batch
devices and to half of this for batch devices. For batch devices, the other
half of the period-size is used as extra buffering to compensate for the delayed
update. So, for batch devices, there is an additional period-size/2 delay.
It makes sense to lower the period-size for batch devices to reduce this delay.

.. code-block:: lua

    ["api.alsa.headroom"] = 0

This adds extra delay between the hardware pointers and software pointers.
In most cases this can be set to 0. For very bad devices or emulated devices
(like in a VM) it might be necessary to increase the headroom value.
In summary, this is the overview of buffering and timings:


  ============== ========================================== =========
  Property       Batch                                      Non-Batch
  ============== ========================================== =========
  IRQ Frequency  api.alsa.period-size/2                     api.alsa.period-size
  Extra Delay    api.alsa.headroom + api.alsa.period-size/2 api.alsa.headroom
  ============== ========================================== =========

It is possible to disable the batch device tweaks with:

.. code-block:: lua

    ["api.alsa.disable-batch"] = true

It removes the extra delay added of period-size/2 if the device can support this.
For batch devices it is also a good idea to lower the period-size
(and increase the IRQ frequency) to get smaller batch updates and lower latency.

ALSA extra latency properties
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Extra internal delay in the DAC and ADC converters of the device itself can be
set with the ``latency.internal.*`` properties:

.. code-block:: lua

    ["latency.internal.rate"] = 256
    ["latency.internal.ns"] = 0

You can configure a latency in samples (relative to rate with
``latency.internal.rate``) or in nanoseconds (``latency.internal.ns``).
This value will be added to the total reported latency by the node of the device.

You can use a tool like ``jack_iodelay`` to get the number of samples of
internal latency of your device.

This property is also adjustable at runtime with the ``ProcessLatency`` param.
You will need to find the id of the Node you want to change. For example:
Query the current internal latency of an ALSA node with id 58:

.. code-block:: console

    $ pw-cli e 58 ProcessLatency
    Object: size 80, type Spa:Pod:Object:Param:ProcessLatency (262156), id Spa:Enum:ParamId:ProcessLatency (16)
      Prop: key Spa:Pod:Object:Param:ProcessLatency:quantum (1), flags 00000000
        Float 0.000000
      Prop: key Spa:Pod:Object:Param:ProcessLatency:rate (2), flags 00000000
        Int 0
      Prop: key Spa:Pod:Object:Param:ProcessLatency:ns (3), flags 00000000
        Long 0

Set the internal latency to 256 samples:

.. code-block:: console

    $ pw-cli s 58 ProcessLatency '{ rate = 256 }'
    Object: size 32, type Spa:Pod:Object:Param:ProcessLatency (262156), id Spa:Enum:ParamId:ProcessLatency (16)
      Prop: key Spa:Pod:Object:Param:ProcessLatency:rate (2), flags 00000000
        Int 256
    remote 0 node 58 changed
    remote 0 port 70 changed
    remote 0 port 72 changed
    remote 0 port 74 changed
    remote 0 port 76 changed

Startup tweaks
^^^^^^^^^^^^^^

Some devices need some time before they can report accurate hardware pointer
positions. In those cases, an extra start delay can be added that is used to
compensate for this startup delay:

.. code-block:: lua

    ["api.alsa.start-delay"] = 0

It is unsure when this tunable should be used.

IEC958 (S/PDIF) passthrough
^^^^^^^^^^^^^^^^^^^^^^^^^^^

S/PDIF passthrough will only be enabled when the accepted codecs are configured
on the ALSA device.

This can be done in 3 different ways:

  1. Use pavucontrol and toggle the codecs in the output advanced section

  2. Modify the ``["iec958.codecs"] = "[ PCM DTS AC3 MPEG MPEG2-AAC EAC3 TrueHD DTS-HD ]"``
     node property to something.

  3. Use ``pw-cli s <node-id> Props '{ iec958Codecs : [ PCM ] }'`` to modify
     the codecs at runtime.
