WirePlumber 0.4.3
~~~~~~~~~~~~~~~~~

Fixes:

  - Implemented logind integration to start the bluez monitor only on the
    WirePlumber instance that is running on the active seat; this fixes a bunch
    of startup warnings and the disappearance of HSP/HFP nodes after login (#54)

  - WirePlumber is now launched with GIO_USE_VFS=local to avoid strange D-Bus
    interference when the user session is restarted, which previously resulted
    in WirePlumber being terminated with SIGTERM and never recovering (#48)

  - WirePlumber now survives a restart of the D-Bus service, reconnecting to
    the bus and reclaiming the bus services that it needs (#55)

  - Implemented route-settings metadata, which fixes storing volume for
    the "System Sounds" in GNOME (#51)

  - Monitor sources can now be selected as the default source (#60)

  - Refactored some policy logic to allow linking to monitors; the policy now
    also respects "stream.capture.sink" property of streams which declares
    that the stream wants to be linked to a monitor (#66)

  - Policy now cleans up 'target.node' metadata so that streams get to follow
    the default source/sink again after the default was changed to match the
    stream's currently configured target (#65)

  - Fixed configuring virtual sources (#57)

  - Device monitors now do not crash if a SPA plugin is missing; instead, they
    print a warning to help users identify what they need to install (!214)

  - Fixed certain "proxy activation failed" warnings (#44)

  - iec958 codec configuration is now saved and restored properly (!228)

  - Fixed some logging issues with the latest version of pipewire (!227, !232)

  - Policy now respects the "node.link-group" property, which fixes issues
    with filter-chain and other virtual sources & sinks (#47)

  - Access policy now grants full permissions to flatpak "Manager" apps (#59)

Policy:

  - Added support for 'no-dsp' mode, which allows streaming audio using the
    format of the device instead of the standard float 32-bit planar format (!225)

Library:

  - WpImplMetadata is now implemented using pw_impl_metadata instead of
    using its own implementation (#52)

  - Added support for custom object property IDs in WpSpaPod (#53)

Misc:

  - Added a script to load the libcamera monitor (!231)

  - Added option to disable building unit tests (!209)

  - WirePlumber will now fail to start with a warning if pipewire-media-session
    is also running in the system (#56)

  - The bluez monitor configuration was updated to match the latest one in
    pipewire-media-session (!224)

Past releases
~~~~~~~~~~~~~

WirePlumber 0.4.2
.................

Highlights:

  - Requires PipeWire 0.3.32 or later at runtime

  - Configuration files are now installed in $PREFIX/share/wireplumber, along
    with scripts, following the paradigm of PipeWire

  - State files are now stored in $XDG_STATE_HOME instead of $XDG_CONFIG_HOME

  - Added new ``file-monitor-api`` module, which allows Lua scripts to watch
    the filesystem for changes, using inotify

  - Added monitor for MIDI devices

  - Added a ``system-lua-version`` meson option that allows distributors to
    choose which Lua version to build against (``auto``, ``5.3`` or ``5.4``)

  - wpipc has been removed and split out to a separate project,
    https://git.automotivelinux.org/src/pipewire-ic-ipc/

Library:

  - A new ``WpImplModule`` class has been added; this allows loading a PipeWire
    module in the WirePlumber process space, keeping a handle that can be
    used to unload that module later. This is useful for loading filters,
    network sources/sinks, etc...

  - State files can now store keys that contain certain GKeyFile-reserved
    characters, such as ``[``, ``]``, ``=`` and space; this fixes storing
    stream volume state for streams using PipeWire's ALSA compatibility PCM
    plugin

  - ``WpProperties`` now uses a boxed ``WpPropertiesItem`` type in its iterators
    so that these iterators can be used with g-i bindings

  - Added API to lookup configuration and script files from multiple places
    in the filesystem

Lua:

  - A ``LocalModule`` API has been added to reflect the functionality offered
    by ``WpImplModule`` in C

  - The ``Node`` API now has a complete set of methods to reflect the methods
    of ``WpNode``

  - Added ``Port.get_direction()``

  - Added ``not-equals`` to the possible constraint verbs

  - ``Debug.dump_table`` now sorts keys before printing the table

Misc:

  - Tests no longer accidentally create files in $HOME; all transient
    files that are used for testing are now created in the build directory,
    except for sockets which are created in ``/tmp`` due to the 108-character
    limitation in socket paths

  - Tests that require optional SPA plugins are now skipped if those SPA plugins
    are not installed

  - Added a nice summary output at the end of meson configuration

  - Documented the Lua ObjectManager / Interest / Constraint APIs

  - Fixed some memory leaks

WirePlumber 0.4.1
.................

Bug fix release to go with PipeWire 0.3.31.
Please update to this version if you are using PipeWire >= 0.3.31.

Highlights:

  - WirePlumber now supports Lua 5.4. You may compile it either with Lua 5.3
    or 5.4, without any changes in behavior. The internal Lua subproject has
    also been upgraded to Lua 5.4, so any builds with ``-Dsystem-lua=false``
    will use Lua 5.4 by default

Fixes:

  - Fixed filtering of pw_metadata objects, which broke with PipeWire 0.3.31

  - Fixed a potential livelock condition in si-audio-adapter/endpoint where
    the code would wait forever for a node's ports to appear in the graph

  - Fixed granting access to camera device nodes in flatpak clients connecting
    through the camera portal

  - Fixed a lot of issues found by the coverity static analyzer

  - Fixed certain race conditions in the wpipc library

  - Fixed compilation with GCC older than v8.1

Scripts:

  - Added a policy script that matches nodes to specific devices based on the
    "media.role" of the nodes and the "device.intended-roles" of the devices

Build system:

  - Bumped GLib requirement to 2.62, as the code was already using 2.62 API

  - Added support for building WirePlumber as a PipeWire subproject

  - Doxygen version requirement has been relaxed to accept v1.8

  - The CI now also verifies that the build works on Ubuntu 20.04 LTS
    and tries multiple builds with different build options

WirePlumber 0.4.0
.................

This is the first stable release of the 0.4.x series, which is expected to be
an API & ABI stable release series to go along with PipeWire 0.3.x. It is
a fundamental goal of this series to maintain compatibility with
pipewire-media-session, making WirePlumber suitable for a desktop PulseAudio &
JACK replacement setup, while supporting other setups as well (ex. automotive)
by making use of its brand new Lua scripting engine, which allows making
customizations easily.

Highlights:

  - Re-implemented the default-routes module in lua, using the same logic
    as the one that pipewire-media-session uses. This fixes a number of issues
    related to volume controls on alsa devices.

  - Implemented a restore-stream lua script, based on the restore-stream
    module from media-session. This allows storing stream volumes and targets
    and restoring them when the stream re-connects

  - Added support for handling dont-remix streams and streams that are not
    autoconnected. Fixes ``pw-cat -p --target=0`` and the gnome-control-center
    channel test

  - Device names are now sanitized in the same way as in pipewire-media-session

  - Disabled endpoints in the default configuration. Using endpoints does
    not provide the best experience on desktop systems yet

  - Fixed a regression introduced in 0.3.96 that would not allow streams to be
    relinked on their endpoints after having been corked by the policy

Library:

  - Some API methods were changed to adhere to the programming practices
    followed elsewhere in the codebase and to be future-proof. Also added
    paddings on public structures so that from this point on, the 0.4.x series
    is going to be API & ABI stable

  - lua: added WpState and wp_metadata_set() bindings and improved
    WpObject.activate() to report errors

  - ObjectManager: added support for declaring interest on all kinds of
    properties of global objects. Previously it was only possible to declare
    interest on pipewire global properties

Misc:

  - daemon & wpexec: changed the exit codes to follow the standardized codes
    defined in sysexits.h

  - wpexec now forces the log level to be >= 1 so that lua runtime errors can be
    printed on the terminal

  - Fixed issues with gobject-introspection data that were introduced by the
    switch to doxygen

  - Fixed a build issue where wp-gtkdoc.h would not be generated in time
    for the gobject-introspection target to build

  - Added a valgrind test setup in meson, use with ``meson test --setup=valgrind``

  - Many memory leak and stability fixes

  - Updated more documentation pages

WirePlumber 0.3.96
..................

Second pre-release (RC2) of WirePlumber 0.4.0.

Highlights:

  - The policy now configures streams for channel upmixing/downmixing

  - Some issues in the policy have been fixed, related to:

    - plugging a new higher priority device while audio is playing
    - pavucontrol creating links to other stream nodes for level monitoring
    - some race condition that could happen at startup

  - Proxy object errors are now handled; this fixes memory leaks of invalid
    links and generally makes things more robust

  - The systemd service units now conflict with pipewire-media-session.service

  - Session & EndpointLink objects have been removed from the API; these were
    not in use after recent refactoring, so they have been removed in order to
    avoid carrying them in the ABI

  - The documentation system has switched to use *Doxygen* & *Sphinx*; some
    documentation has also been updated and some Lua API documentation has
    been introduced

WirePlumber 0.3.95
..................

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

WirePlumber 0.1.2
.................

As shipped in AGL Happy Halibut 8.0.2

WirePlumber 0.1.1
.................

As shipped in AGL Happy Halibut 8.0.1

WirePlumber 0.1.0
.................

First release of WirePlumber, as shipped in AGL Happy Halibut 8.0.0
