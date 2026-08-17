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

The remaining monitor properties are listed under "ALSA PROPERTIES / Monitor
properties" in `pipewire-props(7)`_.

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

Where the properties are documented
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The names that appear inside ``update-props`` are PipeWire object properties.
They are not defined by WirePlumber: they are implemented by the ALSA SPA
plugin and by the audio adapter that PipeWire attaches to every audio node.
Their complete reference is the ``pipewire-props(7)`` man page, which is also
available online at https://docs.pipewire.org/page_man_pipewire-props_7.html

That man page covers the common device properties, the common node properties
(identifying, classifying, scheduling, session manager and format properties),
the audio adapter properties (merger, resampler and channel mixer, including
all the ``channelmix.*`` and ``resample.*`` properties) and the ALSA-specific
device and node properties. Note that only a part of it is ALSA-specific: the
common and audio adapter properties apply to Bluetooth nodes and to
application streams as well.

The two sections below are **a selection only** - the properties that are most
commonly changed on ALSA devices, plus the bits whose behavior is specific to
WirePlumber. For anything that is not listed here, refer to
`pipewire-props(7)`_.

Device properties
^^^^^^^^^^^^^^^^^

A selection of the properties that are commonly set on devices created by the
monitor:

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

.. describe:: device.disabled

   Disables the device. WirePlumber will not create it at all, so it will not
   appear in the list of cards or devices.

   :Type: boolean

.. note::

   Profile and route (ACP "port") selection is handled by WirePlumber itself,
   which is why the ``api.acp.auto-profile`` and ``api.acp.auto-port``
   properties described in `pipewire-props(7)`_ are disabled by default. They
   are only useful in custom configurations where the relevant WirePlumber
   components are disabled. To influence WirePlumber's own choice of profile,
   see `Profile priorities`_ below.

Node properties
^^^^^^^^^^^^^^^

A selection of the properties that are commonly set on nodes created by the
monitor:

.. describe:: node.description

   A user-friendly name for the node. This is what most user interfaces show
   as the name of the sink or source.

   :Type: string

.. describe:: node.disabled

   Disables the node. WirePlumber will not create it at all, so it will not
   appear in the list of nodes.

   :Type: boolean

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

.. describe:: session.suspend-timeout-seconds

   This option configures a different suspend timeout on the node. By default
   this is ``5`` seconds. For some devices (HiFi amplifiers, for example) it
   might make sense to set a higher timeout because they might require some time
   to restart after being idle.

   A value of ``0`` disables suspend for a node and will leave the ALSA device
   busy. The device can then be manually suspended with
   ``pactl suspend-sink|source``.

   :Type: integer

.. describe:: audio.position

   The position of the channels. By default the number of channels and their
   position are determined by the selected device profile. You can override
   this setting here and optionally swap or reconfigure the channel positions.

   :Type: array of strings (example: ``["FL", "FR", "LFE", "FC", "RL", "RR"]``)

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

ALSA MIDI
---------

The ALSA MIDI monitor bridges the ALSA sequencer into PipeWire as a single
node. It is enabled by the ``monitor.alsa-midi`` feature; see
:ref:`config_features`.

.. describe:: monitor.alsa-midi.properties

   Properties for the MIDI bridge node.

   .. code-block::

      monitor.alsa-midi.properties = {
        ## Name set for the node with ALSA MIDI ports
        node.name = "Midi-Bridge"

        ## Removes longname/number from MIDI port names
        api.alsa.disable-longname = false
      }

   The properties that are accepted here are listed under "ALSA MIDI
   PROPERTIES" in `pipewire-props(7)`_.

.. _config_profile_priority_rules:

Profile priorities
------------------

When no profile has been stored for a device and none is forced by other
means, WirePlumber picks the available profile with the highest priority. The
``device.profile.priority.rules`` section allows overriding that choice by
naming an explicit order of preferred profiles for matching devices.

The rules are matched against device properties, and the ``update-props``
action sets a ``priorities`` property holding a JSON array of profile names,
most preferred first:

.. code-block::

   device.profile.priority.rules = [
     {
       matches = [
         { device.name = "~alsa_card.*" }
       ]
       actions = {
         update-props = {
           priorities = [ "pro-audio", "output:analog-stereo" ]
         }
       }
     }
   ]

The first profile in the list that is actually available on the device is
selected. If none of them is available, WirePlumber falls back to picking the
highest priority profile as usual.

.. note::

   This section applies to all devices, not only ALSA ones. For Bluetooth
   devices, the ``bluetooth.profile-preference`` setting provides a simpler way
   of expressing a quality-versus-latency preference; see
   :ref:`config_settings` and :ref:`config_bluetooth`.

.. _pipewire-props(7): https://docs.pipewire.org/page_man_pipewire-props_7.html
