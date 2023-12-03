WirePlumber 0.4.17
~~~~~~~~~~~~~~~~~~

Fixes:

  - Fixed a reference counting issue in the object managers that could cause
    crashes due to memory corruption (#534)

  - Fixed an issue with filters linking to wrong targets, often with two sets
    of links (#536)

  - Fixed a crash in the endpoints policy that would show up when log messages
    were enabled at level 3 or higher

Past releases
~~~~~~~~~~~~~

WirePlumber 0.4.16
..................

Additions:

  - Added a new "sm-objects" script that allows loading objects on demand
    via metadata entries that describe the object to load; this can be used to
    load pipewire modules, such as filters or network sources/sinks, on demand

  - Added a mechanism to override device profile priorities in the configuration,
    mainly as a way to re-prioritize Bluetooth codecs, but this also can be used
    for other devices

  - Added a mechanism in the endpoints policy to allow connecting filters
    between a certain endpoint's virtual sink and the device sink; this is
    specifically intended to allow plugging a filter-chain to act as equalizer
    on the Multimedia endpoint

  - Added wp_core_get_own_bound_id() method in WpCore

Changes:

  - PipeWire 0.3.68 is now required

  - policy-dsp now has the ability to hide hardware nodes behind the DSP sink
    to prevent hardware misuse or damage

  - JSON parsing in Lua now allows keys inside objects to be without quotes

  - Added optional argument in the Lua JSON parse() method to limit recursions,
    making it possible to partially parse a JSON object

  - It is now possible to pass ``nil`` in Lua object constructors that expect an
    optional properties object; previously, omitting the argument was the only
    way to skip the properties

  - The endpoints policy now marks the endpoint nodes as "passive" instead of
    marking their links, adjusting for the behavior change in PipeWire 0.3.68

  - Removed the "passive" property from si-standard-link, since only nodes are
    marked as passive now

Fixes:

  - Fixed the ``wpctl clear-default`` command to completely clear all the
    default nodes state instead of only the last set default

  - Reduced the amount of globals that initially match the interest in the
    object manager

  - Used an idle callback instead of pw_core_sync() in the object manager to
    expose tmp globals

WirePlumber 0.4.15
..................

Additions:

  - A new "DSP policy" module has been added; its purpose is to automatically
    load a filter-chain when a certain hardware device is present, so that
    audio always goes through this software DSP before reaching the device.
    This is mainly to support Apple M1/M2 devices, which require a software
    DSP to be always present

  - WpImplModule now supports loading module arguments directly from a SPA-JSON
    config file; this is mainly to support DSP configuration for Apple M1/M2
    and will likely be reworked for 0.5

  - Added support for automatically combining Bluetooth LE Audio device sets
    (e.g. pairs of earbuds) (!500)

  - Added command line options in ``wpctl`` to display device/node names and
    nicknames instead of descriptions

  - Added zsh completions file for ``wpctl``

  - The device profile selection policy now respects the ``device.profile``
    property if it is set on the device; this is useful to hand-pick a profile
    based on static configuration rules (alsa_monitor.rules)

Changes/Fixes:

  - Linking policy now sends an error to the client before destroying the node,
    if it determines that the node cannot be linked to any target; this fixes
    error reporting on the client side

  - Fixed a crash in suspend-node that could happen when destroying virtual
    sinks that were loaded from another process such as pw-loopback (#467)

  - Virtual machine default period size has been bumped to 1024 (#507)

  - Updated bluez5 default configuration, using ``bluez5.roles`` instead of
    ``bluez5.headset-roles`` now (!498)

  - Disabled Bluetooth autoconnect by default (!514)

  - Removed ``RestrictNamespaces`` option from the systemd services in order to
    allow libcamera to load sandboxed IPA modules (#466)

  - Fixed a JSON encoding bug with empty strings (#471)

  - Lua code can now parse strings without quotes from SPA-JSON

  - Added some missing `\since` annotations and made them show up in the
    generated gobject-introspection file, to help bindings generators

WirePlumber 0.4.14
..................

Additions:

  - Added support for managing Bluetooth-MIDI, complimenting the parts that
    were merged in PipeWire recently (!453)

  - Added a default volume configuration option for streams whose volume
    has never been saved before; that allows starting new streams at a lower
    volume than 100% by default, if desired (!480)

  - Added support for managing link errors and propagating them to the
    client(s) involved. This allows better error handling on the application
    side in case a format cannot be negotiated - useful in video streams
    (see !484, pipewire#2935)

  - snd_aloop devices are now described as being "Loopback" devices
    (pipewire#2214)

  - ALSA nodes in the pro audio profile now get increased graph priority, so
    that they are more likely to become the driver in the graph

  - Added support for disabling libcamera nodes & devices with ``node.disabled``
    and ``device.disabled``, like it works for ALSA and V4L2 (#418)

WirePlumber 0.4.13
..................

Additions:

  - Added bluetooth SCO (HSP/HFP) hardware offload support, together with an
    example script that enables this functionality on the PinePhone

  - Encoded audio (mp3, aac, etc...) can now be passed through, if this mode is
    supported by both the application and the device

  - The v4l2 monitor now also respects the ``node.disabled`` and
    ``device.disabled`` properties inside rules

  - Added "Firefox Developer Edition" to the list of apps that are allowed to
    trigger a bluetooth profile auto-switch (#381)

  - Added support in the portal access script to allow newly plugged cameras
    to be immediately visible to the portal apps

Fixes:

  - Worked around an issue that would prevent streams from properly linking
    when using effects software like EasyEffects and JamesDSP (!450)

  - Fixed destroying pavucontrol-qt monitor streams after the node that was
    being monitored is destroyed (#388)

  - Fixed a crash in the alsa.lua monitor that could happen when a disabled
    device was removed and re-added (#361)

  - Fixed a rare crash in the metadata object (#382)

  - Fixed a bug where a restored node target would override the node target
    set by the application on the node's properties (#335)

Packaging:

  - Added build options to compile wireplumber's library, daemon and tools
    independently

  - Added a build option to disable unit tests that require the dbus daemon

  - Stopped using fakesink/fakesrc in the unit tests to be able to run them
    on default pipewire installations. Compiling the spa ``test`` plugin is no
    longer necessary

  - Added pkg-config and header information in the gir file

WirePlumber 0.4.12
..................

Changes:

  - WirePlumber now maintains a stack of previously configured default nodes and
    prioritizes to one of those when the actively configured default node
    becomes unavailable, before calculating the next default using priorities
    (see !396)

  - Updated bluetooth scripts to support the name changes that happened in
    PipeWire 0.3.59 and also support the experimental Bluetooth LE functionality

  - Changed the naming of bluetooth nodes to not include the profile in it;
    this allows maintaining existing links when switching between a2dp and hfp

  - The default volume for new outputs has changed to be 40% in cubic scale
    (= -24 dB) instead of linear (= 74% cubic / -8 dB) that it was before

  - The default volume for new inputs has changed to be 100% rather than
    following the default for outputs

  - Added ``--version`` flag on the wireplumber executable (#317)

  - Added ``--limit`` flag on ``wpctl set-volume`` to limit the higher volume
    that can be set (useful when incrementing volume with a keyboard shortcut
    that calls into wpctl)

  - The properties of the alsa midi node can now be set in the config files

Fixes:

  - Fixed a crash in lua code that would happen when running in a VM (#303)

  - Fixed a crash that would happen when re-connecting to D-Bus (#305)

  - Fixed a mistake in the code that would cause device reservation not to
    work properly

  - Fixed ``wpctl clear-default`` to accept 0 as a valid setting ID

  - Fixed the logic of choosing the best profile after the active profile
    of a device becomes unavailable (#329)

  - Fixed a regression that would cause PulseAudio "corked" streams to not
    properly link and cause busy loops

  - Fixed an issue parsing spa-json objects that have a nested object as the
    value of their last property

WirePlumber 0.4.11
..................

Changes:

  - The libcamera monitor is now enabled by default, so if the libcamera source
    is enabled in PipeWire, cameras discovered with the libcamera API will be
    available out of the box. This is safe to use alongside V4L2, as long as
    the user does not try to use the same camera over different APIs at the same
    time

  - Libcamera and V4L2 nodes now get assigned a ``priority.session`` number;
    V4L2 nodes get a higher priority by default, so the default camera is going
    to be /dev/video0 over V4L2, unless changed with ``wpctl``

  - Libcamera nodes now get a user-friendly description based on their location
    (ex. built-in front camera). Additionally, V4L2 nodes now have a "(V4L2)"
    string appended to their description in order to be distinguished from
    the libcamera ones

  - 50-alsa-config.lua now has a section where you can set properties that
    will only be applied if WirePlumber is running in a virtual machine. By
    default it now sets ``api.alsa.period-size = 256`` and
    ``api.alsa.headroom = 8192`` (#162, #134)

Fixes:

  - The "enabled" properties in the config files are now "true" by default
    when they are not defined. This fixes backwards compatibility with older
    configuration files (#254)

  - Fixed device name deduplication in the alsa monitor, when device reservation
    is enabled (#241)

  - Reverted a previous fix that makes it possible again to get a glitch when
    changing default nodes while also changing the profile (GNOME Settings).
    The fix was causing other problems and the issue will be addressed
    differently in the future (#279)

  - Fixed an issue that would prevent applications from being moved to a
    recently plugged USB headset (#293)

  - Fixed an issue where wireplumber would automatically link control ports,
    if they are enabled, to audio ports, effectively breaking audio (#294)

  - The policy now always considers the profile of a device that was previously
    selected by the user, if it is available, when deciding which profile to
    activate (#179). This may break certain use cases (see !360)

  - A few documentation fixes

Tools:

  - wpctl now has a ``get-volume`` command for easier scripting of volume controls

  - wpctl now supports relative steps and percentage-based steps in ``set-volume``

  - wpctl now also prints link states

  - wpctl can now ``inspect`` metadata objects without showing critical warnings

Library:

  - A new WpDBus API was added to maintain a single D-Bus connection among
    modules that need one

  - WpCore now has a method to get the virtual machine type, if WirePlumber
    is running in a virtual machine

  - WpSpaDevice now has a ``wp_spa_device_new_managed_object_iterator()`` method

  - WpSpaJson now has a ``wp_spa_json_to_string()`` method that returns a newly
    allocated string with the correct size of the string token

  - WpLink now has a ``WP_LINK_FEATURE_ESTABLISHED`` that allows the caller to
    wait until the link is in the PAUSED or ACTIVE state. This transparently
    now enables watching links for negotiation or allocation errors and failing
    gracefully instead of keeping dead link objects around (#294)

Misc:

  - The Lua subproject was bumped to version 5.4.4

WirePlumber 0.4.10
..................

Changes:

  - Added i18n support to be able to translate some user-visible strings

  - wpctl now supports using ``@DEFAULT_{AUDIO_,VIDEO_,}{SINK,SOURCE}@`` as ID,
    almost like pactl. Additionally, it supports a ``--pid`` flag for changing
    volume and mute state by specifying a process ID, applying the state to all
    nodes of a specific client process

  - The Lua engine now supports loading Lua libraries. These can be placed
    either in the standard Lua libraries path or in the "lib" subdirectory
    of WirePlumber's "scripts" directory and can be loaded with ``require()``

  - The Lua engine's sandbox has been relaxed to allow more functionality
    in scripts (the debug & coroutine libraries and some other previously
    disabled functions)

  - Lua scripts are now wrapped in special WpPlugin objects, allowing them to
    load asynchronously and declare when they have finished their loading

  - Added a new script that provides the same functionality as
    module-fallback-sink from PipeWire, but also takes endpoints into account
    and can be customized more easily. Disabled by default for now to avoid
    conflicts

Policy:

  - Added an optional experimental feature that allows filter-like streams
    (like echo-cancel or filter-node) to match the channel layout of the
    device they connect to, on both sides of the filter; that means that if,
    for instance, a sink has 6 channels and the echo-cancel's source stream
    is linked to that sink, then the virtual sink presented by echo-cancel
    will also be configured to the same 6 channels layout. This feature needs
    to be explicitly enabled in the configuration ("filter.forward-format")

  - filter-like streams (filter-chain and such) no longer follow the default
    sink when it changes, like in PulseAudio

Fixes:

  - The suspend-node script now also suspends nodes that go into the "error"
    state, allowing them to recover from errors without having to restart
    WirePlumber

  - Fixed a crash in mixer-api when setting volume with channelVolumes (#250)

  - logind module now watches only for user state changes, avoiding errors when
    machined is not running

Misc:

  - The configuration files now have comments mentioning which options need to
    be disabled in order to run WirePlumber without D-Bus

  - The configuration files now have properties to enable/disable the monitors
    and other sections, so that it is possible to disable them by dropping in
    a file that just sets the relevant property to false

  - ``setlocale()`` is now called directly instead of relying on ``pw_init()``

  - WpSpaJson received some fixes and is now used internally to parse
    configuration files

  - More applications were added to the bluetooth auto-switch apps whitelist

WirePlumber 0.4.9
.................

Fixes:

  - restore-stream no longer crashes if properties for it are not present
    in the config (#190)

  - spa-json no longer crashes on non-x86 architectures

  - Fixed a potential crash in the bluetooth auto-switch module (#193)

  - Fixed a race condition that would cause Zoom desktop audio sharing to fail
    (#197)

  - Surround sound in some games is now exposed properly (pipewire#876)

  - Fixed a race condition that would cause the default source & sink to not
    be set at startup

  - policy-node now supports the 'target.object' key on streams and metadata

  - Multiple fixes in policy-node that make the logic in some cases behave
    more like PulseAudio (regarding nodes with the dont-reconnect property
    and regarding following the default source/sink)

  - Fixed a bug with parsing unquoted strings in spa-json

Misc:

  - The policy now supports configuring "persistent" device profiles. If a
    device is *manually* set to one of these profiles, then it will not be
    auto-switched to another profile automatically under any circumstances
    (#138, #204)

  - The device-activation module was re-written in lua

  - Brave, Edge, Vivaldi and Telegram were added in the bluetooth auto-switch
    applications list

  - ALSA nodes now use the PCM name to populate node.nick, which is useful
    at least on HDA cards using UCM, where all outputs (analog, hdmi, etc)
    are exposesd as nodes on a single profile

  - An icon name is now set on the properties of bluetooth devices

WirePlumber 0.4.8
.................

Highlights:

  - Added bluetooth profile auto-switching support. Bluetooth headsets will now
    automatically switch to the HSP/HFP profile when making a call and go back
    to the A2DP profile after the call ends (#90)

  - Added an option (enabled by default) to auto-switch to echo-cancel virtual
    device nodes when the echo-cancel module is loaded in pipewire-pulse, if
    there is no other configured default node

Fixes:

  - Fixed a regression that prevented nodes from being selected as default when
    using the pro-audio profile (#163)

  - Fixed a regression that caused encoded audio streams to stall (#178)

  - Fixed restoring bluetooth device profiles

Library:

  - A new WpSpaJson API was added as a front-end to spa-json. This is also
    exposed to Lua, so that Lua scripts can natively parse and write data in
    the spa-json format

Misc:

  - wpctl can now list the configured default sources and sinks and has a new
    command that allows clearing those configured defaults, so that wireplumber
    goes back to choosing the default nodes based on node priorities

  - The restore-stream script now has its own configuration file in
    main.lua.d/40-stream-defaults.lua and has independent options for
    restoring properties and target nodes

  - The restore-stream script now supports rule-based configuration to disable
    restoring volume properties and/or target nodes for specific streams,
    useful for applications that misbehave when we restore those (see #169)

  - policy-endpoint now assigns the "Default" role to any stream that does not
    have a role, so that it can be linked to a pre-configured endpoint

  - The route-settings-api module was dropped in favor of dealing with json
    natively in Lua, now that the API exists

WirePlumber 0.4.7
.................

Fixes:

  - Fixed a regression in 0.4.6 that caused the selection of the default audio
    sources and sinks to be delayed until some event, which effectively caused
    losing audio output in many circumstances (#148, #150, #151, #153)

  - Fixed a regression in 0.4.6 that caused the echo-cancellation pipewire
    module (and possibly others) to not work

  - A default sink or source is now not selected if there is no available route
    for it (#145)

  - Fixed an issue where some clients would wait for a bit while seeking (#146)

  - Fixed audio capture in the endpoints-based policy

  - Fixed an issue that would cause certain lua scripts to error out with older
    configuration files (#158)

WirePlumber 0.4.6
.................

Changes:

  - Fixed a lot of race condition bugs that would cause strange crashes or
    many log messages being printed when streaming clients would connect and
    disconnect very fast (#128, #78, ...)

  - Improved the logic for selecting a default target device (#74)

  - Fixed switching to headphones when the wired headphones are plugged in (#98)

  - Fixed an issue where ``udevadm trigger`` would break wireplumber (#93)

  - Fixed an issue where switching profiles of a device could kill client nodes

  - Fixed briefly switching output to a secondary device when switching device
    profiles (#85)

  - Fixed ``wpctl status`` showing default device selections when dealing with
    module-loopback virtual sinks and sources (#130)

  - WirePlumber now ignores hidden files from the config directory (#104)

  - Fixed an interoperability issue with jackdbus (pipewire#1846)

  - Fixed an issue where pulseaudio tcp clients would not have permissions to
    connect to PipeWire (pipewire#1863)

  - Fixed a crash in the journald logger with NULL debug messages (#124)

  - Enabled real-time priority for the bluetooth nodes to run in RT (#132)

  - Made the default stream volume configurable

  - Scripts are now also looked up in $XDG_CONFIG_HOME/wireplumber/scripts

  - Updated documentation on configuring WirePlumber and fixed some more
    documentation issues (#68)

  - Added support for using strings as log level selectors in WIREPLUMBER_DEBUG

WirePlumber 0.4.5
.................

Fixes:

  - Fixed a crash that could happen after a node linking error (#76)

  - Fixed a bug that would cause capture streams to link to monitor ports
    of loopback nodes instead of linking to their capture ports

  - Fixed a needless wait that would happen on applications using the pipewire
    ALSA plugin (#92)

  - Fixed an issue that would cause endless rescan loops in policy-node and
    could potentially also cause other strange behaviors in case pavucontrol
    or another monitoring utility was open while the policy was rescanning (#77)

  - Fixed the endpoints-based policy that broke in recent versions and improved
    its codebase to share more code and be more in-line with policy-node

  - The semicolon character is now escaped properly in state files (#82)

  - When a player requests encoded audio passthrough, the policy now prefers
    linking to a device that supports that instead of trying to link to the
    default device and potentially failing (#75)

  - Miscellaneous robustness fixes in policy-node

API:

  - Added WpFactory, a binding for pw_factory proxies. This allows object
    managers to query factories that are loaded in the pipewire daemon

  - The file-monitor-api plugin can now watch files for changes in addition
    to directories

WirePlumber 0.4.4
.................

Highlights:

  - Implemented linking nodes in passthrough mode, which enables encoded
    iec958 / dsd audio passthrough

  - Streams are now sent an error if it was not possible to link them to
    a target (#63)

  - When linking nodes where at least one of them has an unpositioned channel
    layout, the other one is not reconfigured to match the channel layout;
    it is instead linked with a best effort port matching logic

  - Output route switches automatically to the latest one that has become
    available (#69)

  - Policy now respects the 'node.exclusive' and 'node.passive' properties

  - Many other minor policy fixes for a smoother desktop usage experience

API:

  - Fixed an issue with the ``LocalModule()`` constructor not accepting ``nil``
    as well as the properties table properly

  - Added ``WpClient.send_error()``, ``WpSpaPod.fixate()`` and
    ``WpSpaPod.filter()`` (both in C and Lua)

Misc:

  - Bumped meson version requirement to 0.56 to be able to use
    ``meson.project_{source,build}_root()`` and ease integration with pipewire's
    build system as a subproject

  - wireplumber.service is now an alias to pipewire-session-manager.service

  - Loading the logind module no longer fails if it was not found on the system;
    there is only a message printed in the output

  - The logind module can now be compiled with elogind (#71)

  - Improvements in wp-uninstalled.sh, mostly to ease its integration with
    pipewire's build system when wireplumber is build as a subproject

  - The format of audio nodes is now selected using the same algorithm as in
    media-session

  - Fixed a nasty segfault that appeared in 0.4.3 due to a typo (#72)

  - Fixed a re-entrancy issue in the wplua runtime (#73)

WirePlumber 0.4.3
.................

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
