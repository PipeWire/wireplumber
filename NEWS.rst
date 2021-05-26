WirePlumber 0.3.95
~~~~~~~~~~~~~~~~~~

First pre-release (RC1) of WirePlumber 0.4.0.

Highlights:

  - Lua scripting engine. All the session management logic is now scripted
    and there is also the ability to run scripts standalone with ``wpexec``
    (see tests/examples).

  - Compatibility with the latest PipeWire (0.3.26+ required). Also, most
    features and behavioral logic of pipewire-media-session 0.3.26 are
    available, making WirePlumber suitable for a desktop PulseAudio & JACK
    replacement setup.

  - Compatibility with embedded system policies, like the one on AGL, has been
    restored and is fully configurable.

  - The design of endpoints has been simplified. We now associate endpoints
    with use cases (roles) instead of physical devices. This removes the need
    for "endpoint stream" objects, allows more logic to be scripted in lua
    and makes the graph simpler. It is also possible to run without endpoints
    at all, matching the behavior of pipewire-media-session and pulseaudio.

  - Configuration is now done using a pipewire-style json .conf file plus lua
    files. Most of the options go in the lua files, while pipewire context
    properties, spa_libs and pipewire modules are configured in the json file.

  - Systemd unit files have been added and are the recommended way to run
    wireplumber. Templated unit files are also available, which allow running
    multiple instances of wireplumber with a specific configuration each.

Past releases
~~~~~~~~~~~~~

WirePlumber 0.3.0
.................

The desktop-ready release!

Changes since 0.2.96:

  - Changed how the device endpoints & nodes are named
    to make them look better in JACK graph tools, such as qjackctl.
    JACK tools use the ':' character as a separator to distinguish the node
    name from the port name (since there are no actual nodes in JACK) and
    having ':' in our node names made the graph look strange in JACK

  - Fixed an issue with parsing wireplumber.conf that could cause
    out-of-bounds memory access

  - Fixed some pw_proxy object leaks that would show up in the log

  - Fixed more issues with unlinking the stream volume (si-convert) node
    from the ALSA sink node and suspending the both;
    This now also works with PipeWire 0.3.5 and 0.3.6, so it is possible
    to use these PipeWire versions with WirePlumber without disabling streams
    on audio sinks.

WirePlumber 0.2.96
..................

Second pre-release (RC2) of WirePlumber 0.3.0

Changes since 0.2.95:

  - Quite some work went into fixing bugs related to the ``ReserveDevice1``
    D-Bus API. It is now possible to start a JACK server before or after
    WirePlumber and WirePlumber will automatically stop using the device that
    JACK opens, while at the same time it will enable the special "JACK device"
    that allows PipeWire to interface with JACK

  - Fixed a number of issues that did not previously allow using the spa
    bluez5 device with WirePlumber. Now it is possible to at least use the
    A2DP sink (output to bluetooth speakers) without major issues

  - On the API level, ``WpCore`` was changed to allow having multiple instances
    that share the same ``pw_context``. This is useful to have multiple
    connections to PipeWire, while sharing the context infrastructure

  - ``WpCore`` also gained support for retrieving server info & properties
    and ``wpctl status`` now also prints info about the server & all clients

  - ``module-monitor`` was modified to allow loading multiple monitor instances
    with one instance of the module itself

  - Audio nodes are now configured with the sample rate that is defined
    globally in ``pipewire.conf`` with ``set-prop default.clock.rate <rate>``

  - Policy now respects the ``node.autoconnect`` property; additionally, it is
    now possible to specify endpoint ids in the ``node.target`` property of nodes
    (so endpoint ids are accepted in the ``PIPEWIRE_NODE`` environment variable,
    and in the ``path`` property of the pipewire gstreamer elements)

  - Fixed an issue where links between the si-convert audioconvert nodes and
    the actual device nodes would stay active forever; they are now declared
    as "passive" links, which allows the nodes to suspend. This requires
    changes to PipeWire that were commited after 0.3.6; when using WirePlumber
    with 0.3.5 or 0.3.6, it is recommended to disable streams on audio sinks
    by commenting out the ``streams = "audio-sink.streams"`` lines in the
    .endpoint configuration files

  - ``wireplumber.conf`` now accepts comments to be present inside blocks and
    at the end of valid configuration lines

  - Improved documentation and restructured the default configuration to be
    more readable and sensible

  - Fixed issues that prevented using WirePlumber with GLib < 2.60;
    2.58 is now the actual minimum requirement

WirePlumber 0.2.95
..................

First pre-release of WirePlumber 0.3.0.

This is the first release that targets desktop use-cases. It aims to be
fully compatible with ``pipewire-media-session``, while at the same time it
adds a couple of features that ``pipewire-media-session`` lacks, such as:

  - It makes use of session, endpoint and endpoint-stream objects
    to orchestrate the graph

  - It is configurable:

    - It supports configuration of endpoints, so that their properties
      (such as their name) can be overriden

    - It also supports declaring priorities on endpoints, so that there
      are sane defaults on the first start

    - It supports partial configuration of linking policy

    - It supports creating static node and device objects at startup,
      also driven by configuration files

  - It has the concept of session default endpoints, which can be changed
    with ``wpctl`` and are stored in XDG_CONFIG_DIR, so the user may change
    at runtime the target device of new links in a persistent way

  - It supports volume & mute controls on audio endpoints, which can be
    set with ``wpctl``

  - Last but not least, it is extensible

Also note that this release currently breaks compatibility with AGL, since
the policy management engine received a major refactoring to enable more
use-cases, and has been focusing on desktop support ever since.
Policy features specific to AGL and other embedded systems are expected
to come back in a 0.3.x point release.

WirePlumber 0.2.0
.................

As shipped in AGL Itchy Icefish 9.0.0 and Happy Halibut 8.0.5

WirePlumber 0.1.1
.................

As shipped in AGL Happy Halibut 8.0.2

WirePlumber 0.1.1
.................

As shipped in AGL Happy Halibut 8.0.1

WirePlumber 0.1.0
.................

First release of WirePlumber, as shipped in AGL Happy Halibut 8.0.0
