wpctl(1)
========

SYNOPSIS
--------

**wpctl** [*COMMAND*] [*COMMAND_OPTIONS*]

DESCRIPTION
-----------

**wpctl** is a command-line control tool for WirePlumber, the PipeWire session
manager. It provides an interface to inspect, control, and configure audio and
video devices, nodes, and their properties within a PipeWire media server.

WirePlumber manages audio and video routing, device configuration, and session
policies. **wpctl** allows users to interact with these components, change
volume levels, set default devices, inspect object properties, and modify
settings.

COMMANDS
--------

status
^^^^^^

**wpctl status** [**-k**\|\ **--nick**] [**-n**\|\ **--name**]

Displays the current state of objects in PipeWire, including devices, sinks,
sources, filters, and streams. Shows a hierarchical view of the audio/video
system.

Options:
  **-k**, **--nick**
    Display device and node nicknames instead of descriptions
  **-n**, **--name**
    Display device and node names instead of descriptions

get-volume
^^^^^^^^^^

**wpctl get-volume** *ID*

Displays volume information about the specified node, including current volume
level and mute state.

Arguments:
  *ID*
    Node ID or special identifier (see `SPECIAL IDENTIFIERS`_)

inspect
^^^^^^^

**wpctl inspect** *ID* [**-r**\|\ **--referenced**] [**-a**\|\ **--associated**]

Displays detailed information about the specified object, including all
properties and metadata.

Arguments:
  *ID*
    Object ID or special identifier

Options:
  **-r**, **--referenced**
    Show objects that are referenced in properties
  **-a**, **--associated**
    Show associated objects

set-default
^^^^^^^^^^^

**wpctl set-default** *ID*

Sets the specified device node to be the default target of its kind (capture or
playback) for new streams that require auto-connection.

Arguments:
  *ID*
    Sink or source node ID

set-volume
^^^^^^^^^^

**wpctl set-volume** *ID* *VOL*\ [**%**]\ [**-**\|\ **+**] [**-p**\|\ **--pid**] [**-l** *LIMIT*\|\ **--limit** *LIMIT*]

Sets the volume of the specified node.

Arguments:
  *ID*
    Node ID, special identifier, or PID (with --pid)
  *VOL*\ [**%**]\ [**-**\|\ **+**]
    Volume specification:

    - *VOL* - Set volume to specific value (1.0 = 100%)
    - *VOL*\ **%** - Set volume to percentage (50% = 0.5)
    - *VOL*\ **+** - Increase volume by value
    - *VOL*\ **-** - Decrease volume by value
    - *VOL*\ **%+** - Increase volume by percentage
    - *VOL*\ **%-** - Decrease volume by percentage

Options:
  **-p**, **--pid**
    Treat ID as a process ID and affect all nodes associated with it
  **-l** *LIMIT*, **--limit** *LIMIT*
    Limit final volume to below this value (floating point, 1.0 = 100%)

Examples:
  Set volume to 50%: ``wpctl set-volume @DEFAULT_SINK@ 0.5``

  Increase volume by 10%: ``wpctl set-volume 42 10%+``

  Set volume for all nodes of PID 1234: ``wpctl set-volume --pid 1234 0.8``

set-mute
^^^^^^^^

**wpctl set-mute** *ID* **1**\|\ **0**\|\ **toggle** [**-p**\|\ **--pid**]

Changes the mute state of the specified node.

Arguments:
  *ID*
    Node ID, special identifier, or PID (with --pid)
  **1**\|\ **0**\|\ **toggle**
    Mute state: 1 (mute), 0 (unmute), or toggle current state

Options:
  **-p**, **--pid**
    Treat ID as a process ID and affect all nodes associated with it

set-profile
^^^^^^^^^^^

**wpctl set-profile** *ID* *INDEX*

Sets the profile of the specified device to the given index.

Arguments:
  *ID*
    Device ID or special identifier
  *INDEX*
    Profile index (integer, 0 typically means 'off')

set-route
^^^^^^^^^

**wpctl set-route** *ID* *INDEX*

Sets the route of the specified device to the given index.

Arguments:
  *ID*
    Device node ID or special identifier
  *INDEX*
    Route index (integer, 0 typically means 'off')

clear-default
^^^^^^^^^^^^^

**wpctl clear-default** [*ID*]

Clears the default configured node. If no ID is specified, clears all default
nodes.

Arguments:
  *ID* (optional)
    Settings ID to clear (0-2 for Audio/Sink, Audio/Source, Video/Source).
    If omitted, clears all defaults.

settings
^^^^^^^^

**wpctl settings** [*KEY*] [*VAL*] [**-d**\|\ **--delete**] [**-s**\|\ **--save**] [**-r**\|\ **--reset**]

Shows, changes, or removes WirePlumber settings.

Arguments:
  *KEY* (optional)
    Setting key name
  *VAL* (optional)
    Setting value (JSON format)

Options:
  **-d**, **--delete**
    Delete the saved setting value (no KEY means delete all)
  **-s**, **--save**
    Save the setting value (no KEY means save all, no VAL means current value)
  **-r**, **--reset**
    Reset the setting to its default value

Behavior:
  - No arguments: Show all settings
  - KEY only: Show specific setting value
  - KEY and VAL: Set specific setting value

set-log-level
^^^^^^^^^^^^^

**wpctl set-log-level** [*ID*] *LEVEL*

Sets the log level of a client.

Arguments:
  *ID* (optional)
    Client ID. If omitted, applies to WirePlumber. Use 0 for PipeWire server.
  *LEVEL*
    Log level (e.g., ``0``, ``1``, ``2``, ``3``, ``4``, ``5``, ``E``, ``W``, ``N``, ``I``, ``D``, ``T``).
    Use ``-`` to unset the log level.

SPECIAL IDENTIFIERS
-------------------

Instead of numeric IDs, **wpctl** accepts these special identifiers for
commonly used defaults:

**@DEFAULT_SINK@**, **@DEFAULT_AUDIO_SINK@**
  The current default audio sink (playback device)

**@DEFAULT_SOURCE@**, **@DEFAULT_AUDIO_SOURCE@**
  The current default audio source (capture device)

**@DEFAULT_VIDEO_SOURCE@**
  The current default video source (camera)

These identifiers are resolved at runtime to the appropriate node IDs.

EXIT STATUS
-----------

**wpctl** returns the following exit codes:

0
  Success
1
  General error (e.g., invalid arguments, connection failure)
2
  Could not connect to PipeWire
3
  Command-specific error (e.g., object not found)

EXAMPLES
--------

Display system status::

    wpctl status

Set default audio sink::

    wpctl set-default 42

Set volume to 75% on default sink::

    wpctl set-volume @DEFAULT_SINK@ 75%

Increase volume by 5% on a specific node::

    wpctl set-volume 42 5%+

Mute the default source::

    wpctl set-mute @DEFAULT_SOURCE@ 1

Toggle mute on default sink::

    wpctl set-mute @DEFAULT_SINK@ toggle

Inspect a device with associated objects::

    wpctl inspect --associated 30

Show all WirePlumber settings::

    wpctl settings

Set a specific setting::

    wpctl settings bluetooth.autoswitch true

Save all current settings::

    wpctl settings --save

Set log level for WirePlumber to debug::

    wpctl set-log-level D

Set log level for a specific client::

    wpctl set-log-level 42 W

NOTES
-----

Object IDs can be found using the **status** command. The hierarchical display
shows IDs for devices, nodes, and other objects.

Volume values are floating-point numbers where 1.0 represents 100% volume.
Values can exceed 1.0 to introduce volume amplification.

When using the **--pid** option, **wpctl** will find all audio nodes associated
with the specified process ID and apply the operation to all of them.

SEE ALSO
--------

**pipewire**\ (1), **pw-cli**\ (1), **pw-dump**\ (1), **wireplumber**\ (1)

WirePlumber Documentation: https://pipewire.pages.freedesktop.org/wireplumber/
