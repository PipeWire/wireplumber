.. _config_alsa:

ALSA configuration
==================

One of the components of WirePlumber is the ALSA monitor. This monitor is
responsible for creating PipeWire devices and nodes for all the ALSA cards that
are available on the system. It also manages the configuration of these devices.

The ALSA monitor is enabled by default and can be disabled using the
``monitor.alsa`` :ref:`feature <config_features>` in the configuration file.

The monitor, as with all device monitors, is implemented as a SPA plugin and is
part of PipeWire. WirePlumber merely loads the plugin and lets it do its work.
The plugin then monitors UDev and creates device and node objects for all the
ALSA cards that are available on the system.

.. note::

   One thing worth remembering here is that in ALSA, a "card" represents a
   physical sound controller device, and a "device" is a logical access point
   that represents a set of inputs and/or outputs that are part of the card. In
   PipeWire, a "device" is the direct equivalent of an ALSA "card" and a "node"
   is almost equivalent (close, but not quite) of an ALSA "device".

Properties
----------

The ALSA monitor SPA plugin (``api.alsa.enum.udev``) supports properties that
can be used to configure it when it is loaded. These properties can be set in
the ``monitor.alsa.properties`` section of the WirePlumber configuration file.

Example:

.. code-block::

   monitor.alsa.properties = {
     alsa.use-acp = true
   }

.. describe:: alsa.use-acp

   A boolean that controls whether the ACP (alsa card profile) code is to be
   the default manager of the device. This will probe the device and configure
   the available profiles, ports and mixer settings. The code to do this is
   taken directly from PulseAudio and provides devices that look and feel
   exactly like the PulseAudio devices.

Rules
-----

When device and node objects are created by the ALSA monitor, they can be
configured using rules. These rules allow matching the existing properties of
these objects and updating them with new values. This is the main way of
configuring ALSA device settings.

These rules can be set in the ``monitor.alsa.rules`` section of the WirePlumber
configuration file.

Example:

.. code-block::

   monitor.alsa.rules = [
     {
       matches = [
         {
           # This matches the value of the 'device.name' property of the device.
           device.name = "~alsa_card.*"
         }
       ]
       actions = {
         update-props = {
           # Apply all the desired device settings here.
           api.alsa.use-acp = true
         }
       }
     }
     {
       matches = [
         # This matches the value of the 'node.name' property of the node.
         {
           node.name = "~alsa_output.*"
         }
       ]
       actions = {
         # Apply all the desired node specific settings here.
         update-props = {
           node.nick              = "My Node"
           priority.driver        = 100
           session.suspend-timeout-seconds = 5
         }
       }
     }
   ]

Device properties
^^^^^^^^^^^^^^^^^

The following properties can be configured on devices created by the monitor:

.. describe:: api.alsa.use-acp

   Use the ACP (alsa card profile) code to manage this device. This will probe
   the device and configure the available profiles, ports and mixer settings.
   The code to do this is taken directly from PulseAudio and provides devices
   that look and feel exactly like the PulseAudio devices.

   :Default value: ``true``
   :Type: boolean

.. describe:: api.alsa.use-ucm

   When ACP is enabled and a UCM configuration is available for a device, by
   default it is used instead of the ACP profiles. This option allows you to
   disable this and use the ACP profiles instead.

   This option does nothing if ``api.alsa.use-acp`` is set to ``false``.

   :Default value: ``true``
   :Type: boolean

.. describe:: api.alsa.soft-mixer

   Setting this option to ``true`` will disable the hardware mixer for volume
   control and mute. All volume handling will then use software volume and mute,
   leaving the hardware mixer untouched. The hardware mixer will still be used
   to mute unused audio paths in the device.

   :Type: boolean

.. describe:: api.alsa.ignore-dB

   Setting this option to ``true`` will ignore the decibel setting configured by
   the driver. Use this when the driver reports wrong settings.

   :Type: boolean

.. describe:: device.profile-set

   This option can be used to select a custom ACP profile-set name for the
   device. This can be configured in UDev rules, but it can also be specified
   here. The default is to use "default.conf".

   :Type: string

.. describe:: device.profile

   The initial active profile name. The default is to start from the "Off"
   profile and then let WirePlumber select the best profile based on its
   policy.

   :Type: string

.. describe:: api.acp.auto-profile

   Automatically select the best profile for the device. Normally this option is
   disabled because WirePlumber will manage the profile of the device.
   WirePlumber can save and load previously selected profiles. Enable this in
   custom configurations where the relevant WirePlumber components are disabled.

   :Type: boolean

.. describe:: api.acp.auto-port

   Automatically select the highest priority port that is available ("port" is a
   PulseAudio/ACP term, the equivalent of a "Route" in PipeWire). This is by
   default disabled because WirePlumber handles the task of selecting and
   restoring Routes. Enable this in custom configurations where the relevant
   WirePlumber components are disabled.

   :Type: boolean

.. describe:: api.acp.probe-rate

   Sets the samplerate used for probing the ALSA devices and collecting the
   profiles and ports.

   :Type: integer

.. describe:: api.acp.pro-channels

   Sets the number of channels to use when probing the "Pro Audio" profile.
   Normally, the maximum amount of channels will be used but with this setting
   this can be reduced, which can make it possible to use other samplerates on
   some devices.

   :Type: integer

Some of the other properties that can be configured on devices:

.. describe:: device.nick

   A short name for the device.

.. describe:: device.description

   A longer, user-friendly name of the device. This will show up in most
   user interfaces as the device's name.

Node properties
^^^^^^^^^^^^^^^

The following properties can be configured on nodes created by the monitor:

.. describe:: priority.driver

   This configures the node driver priority. Nodes with higher priority will be
   used as a driver in the graph. Other nodes with lower priority will have to
   resample to the driver node when they are joined in the same graph. The
   default value is set based on some heuristics.

   :Type: integer

.. describe:: priority.session

   This configures the priority of the node when selecting a default node
   (default sink/source as a link target for streams). Higher priority nodes
   will be more likely candidates for becoming the default node.

   :Type: integer

   .. note::

      By default, sources have a ``priority.session`` value around 1600-2000 and
      sinks have a value around 600-1000. If you are increasing the priority of
      a sink, it is **not advised** to use a value higher than 1500, as it may
      cause a sink's monitor to be selected as the default source.

.. describe:: node.pause-on-idle

   Pause the node when nothing is linked to it anymore. This is by default false
   because some devices make a "pop" sound when they are opened/closed.
   The node will normally pause and suspend after a timeout (see below).

   :Type: boolean

.. describe:: session.suspend-timeout-seconds

   This option configures a different suspend timeout on the node. By default
   this is ``5`` seconds. For some devices (HiFi amplifiers, for example) it
   might make sense to set a higher timeout because they might require some time
   to restart after being idle.

   A value of ``0`` disables suspend for a node and will leave the ALSA device
   busy. The device can then be manually suspended with
   ``pactl suspend-sink|source``.

   :Type: integer

.. describe:: audio.format

   The sample format of the device. By default, PipeWire will use a 32 bits
   sample format but a different format can be set here.

   :Type: string (``"S16LE"``, ``"S32LE"``, ``"F32LE"``, ...)

.. describe:: audio.rate

   The sample rate of the device. By default, the ALSA device will be configured
   with the same samplerate as the global graph. If this is not supported, or a
   custom value is set here, resampling will be used to match the graph rate.

   :Type: integer

.. describe:: audio.channels

   The number of channels of the device. By default the channels and their
   position are determined by the selected device profile. You can override
   this setting here.

   :Type: integer

.. describe:: audio.position

   The position of the channels. By default the number of channels and their
   position are determined by the selected device profile. You can override
   this setting here and optionally swap or reconfigure the channel positions.

   :Type: array of strings (example: ``["FL", "FR", "LFE", "FC", "RL", "RR"]``)

.. describe:: api.alsa.use-chmap

    Use the channel map as reported by the driver. This is disabled by default
    because it is often wrong and the ACP code handles this better.

    :Type: boolean

.. describe:: api.alsa.disable-mmap

   Disable the use of mmap for the ALSA device. By default, PipeWire will access
   the memory of the device using mmap. This can be disabled and force the usage
   of the slower read and write access modes, in case the mmap support of the
   device is not working properly.

   :Type: boolean

.. describe:: channelmix.normalize

   Normalize the channel volumes when mixing & resampling, making sure that the
   original 0 dB level is preserved so that nothing sounds wildly
   quieter/louder. This is disabled by default.

   :Type: boolean

.. describe:: channelmix.mix-lfe

   Creates a "center" channel for X.0 recordings from the front stereo on X.1
   setups and pushes some low-frequency/bass from the "center" of X.1 recordings
   into the front stereo on X.0 setups. This is disabled by default.

   :Type: boolean

.. describe:: monitor.channel-volumes

   By default, the volume of the sink/source does not influence the volume on
   the monitor ports. Set this option to true to change this. PulseAudio has
   inconsistent behaviour regarding this option, it applies channel-volumes only
   when the sink/source is using software volumes.

   :Type: boolean

ALSA buffer properties
......................

PipeWire by default uses a timer to consume and produce samples to/from ALSA
devices. After every timeout, it queries the hardware pointers of the device and
uses this information to set a new timeout. This works well for most devices,
but there is a class of devices, so called "batch" devices, that need extra
buffering and timing tweaks to work properly. This is because batch devices only
get their hardware pointers updated after each hardware interrupt. When the
hardware interrupt frequency and the timer frequency are aligned, it is possible
for the hardware pointers to be updated just after the timer has expired,
resulting in sometimes wrong timing information being returned by the query. In
contrast, non-batch devices get pointer updates independent of the interrupt.

This means that for batch devices we need to set the interrupt at a sufficiently
high frequency, at the cost of CPU usage, while for non-batch devices we want to
set the interrupt frequency as low as possible to save CPU. For batch devices
we also need to take the extra buffering into account caused by the delayed
updates of the hardware pointers.

.. note::

   Most USB devices are batch devices and will be handled as such by PipeWire by
   default.

There are 2 tunable parameters to control the buffering and timeouts in a
device:

.. describe:: api.alsa.period-size

   This sets the device interrupt to every period-size samples for non-batch
   devices and to half of this for batch devices. For batch devices, the other
   half of the period-size is used as extra buffering to compensate for the
   delayed update. So, for batch devices, there is an additional period-size/2
   delay. It makes sense to lower the period-size for batch devices to reduce
   this delay.

   :Type: integer (samples)

.. describe:: api.alsa.headroom

   This adds extra delay between the hardware pointers and software pointers.
   In most cases this can be set to 0. For very bad devices or emulated devices
   (like in a VM) it might be necessary to increase the headroom value.

   :Type: integer (samples)

.. describe:: api.alsa.period-num

   This configures the number of periods in the hardware buffer, which controls
   its size. Note that this is multiplied by the period of the device to
   determine the size, so for batch devices, the total buffer size is
   effectively period-num * period-size/2.

   :Type: integer

In summary, this is the overview of buffering and timings:

============== ============================================ ==========================================
Property       Batch                                        Non-Batch
============== ============================================ ==========================================
IRQ Frequency  api.alsa.period-size/2                       api.alsa.period-size
Extra Delay    api.alsa.headroom + api.alsa.period-size/2   api.alsa.headroom
Buffer Size    api.alsa.period-num * api.alsa.period-size/2 api.alsa.period-num * api.alsa.period-size
============== ============================================ ==========================================

Finally, it is possible to disable the batch device tweaks with:

.. describe:: api.alsa.disable-batch

   This disables the batch device tweaks. It removes the extra delay added of
   period-size/2 if the device can support this. For batch devices it is also a
   good idea to lower the period-size (and increase the IRQ frequency) to get
   smaller batch updates and lower latency.

   :Type: boolean

ALSA extra latency properties
.............................

Extra internal delay in the DAC and ADC converters of the device itself can be
set with the ``latency.internal.*`` properties:

.. code-block::

    latency.internal.rate = 256
    latency.internal.ns = 0

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
..............

.. describe:: api.alsa.start-delay

   Some devices need some time before they can report accurate hardware pointer
   positions. In those cases, an extra start delay can be added to compensate
   for this startup delay. This sets the startup delay in samples. The default
   is 0.

   :Type: integer (samples)

IEC958 (S/PDIF) passthrough
...........................

.. describe:: iec958.codecs

   S/PDIF passthrough will only be enabled when the accepted codecs are configured
   on the ALSA device. This can be done by setting the list of supported codecs
   on this property.

   Note that it is possible to also configure this property at runtime, either
   with tools like pavucontrol or with the ``pw-cli`` tool, like this:
   ``pw-cli s <node-id> Props '{ iec958Codecs : [ PCM ] }'``

   :Type: array of strings (example: ``[ "PCM", "DTS", "AC3", "EAC3", "TrueHD", "DTS-HD" ]``)
