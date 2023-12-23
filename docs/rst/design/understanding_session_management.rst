.. _design_understanding_session_management:

Understanding Session Management
================================

The PipeWire session manager is a tool that is tasked to do a lot of things.
Many people understand the term "session manager" as a tool that is responsible
for managing links between nodes, but that is only one of many tasks. To
understand the entirety of its operation, we need to discuss how PipeWire works,
first.

When PipeWire starts, it loads a set of modules that are defined in its
configuration file. These modules provide functionality to PipeWire, otherwise
it is just an empty process that does nothing. Under normal circumstances,
the modules that are loaded on PipeWire's startup contain object *factories*,
plus the native protocol module that allows inter-process communication.
Other than that, PipeWire does not really load or do anything else. This is
where session management begins.

Session management is basically about setting up PipeWire to do something
useful. This is achieved by utilizing PipeWire's exposed object factories to
create some useful objects, then work with their methods to modify and later
destroy them. Such objects include devices, nodes, ports, links and others.
This is a task that requires continuous monitoring and action taking, reacting
on a large number of different events that happen as the system is being used.

High-level areas of operation
-----------------------------

The session management logic, in WirePlumber, is divided into 6 different areas
of operation:

1. Device Enablement
^^^^^^^^^^^^^^^^^^^^

Enabling devices is a fundamental area of operation. It is achieved by using
the device monitor objects (or just "monitors"), which are typically
implemented as SPA plugins in PipeWire, but they are loaded by WirePlumber.
Their task is to discover available media devices and create objects in PipeWire
that offer a way to interact with them.

Well-known monitors include:

  - The ALSA monitor, which enables audio devices
  - The ALSA MIDI monitor, which enables MIDI devices
  - The libcamera monitor, which enables cameras
  - The Video4Linux2 (V4L2) monitor, which also enables cameras, but also
    other video capture devices through the V4L2 Linux API
  - The BlueZ monitor, which enables bluetooth audio devices

2. Device Configuration
^^^^^^^^^^^^^^^^^^^^^^^

Most devices expose complex functionality, from the computer's perspective, that
needs to be managed in order to provide a simple and smooth user experience.
For that reason, for example, audio devices are organized into *profiles* and
*routes*, which allow setting them up to serve a specific use case. These
need to be configured and managed by the session manager.

3. Client Access Control
^^^^^^^^^^^^^^^^^^^^^^^^

When client applications connect to PipeWire, they need to obtain permissions
in order to be able to access the objects exposed by PipeWire and interact
with them. In some circumstances and configurations, the session manager is also
tasked with deciding which permissions should be granted to each client.

4. Node Configuration
^^^^^^^^^^^^^^^^^^^^^

Nodes are the fundamental elements of media processing. They are typically
created either by the device monitors or by client applications. When they are
created, they are in a state where they cannot be linked. Linking them requires
some configuration, such as configuring the media format and subsequently
the number and the type of ports that should be exposed. Additionally, some
properties and metadata related to the node might need to be set according to
user preferences. All of this is taken care of by the session manager.

5. Link Management
^^^^^^^^^^^^^^^^^^

When nodes are finally ready to use, the session manager is also tasked to
decide how they should be linked together in order for media to flow though.
For instance, an audio playback stream node most likely needs to be linked to
the default audio output device node. The session manager then also needs to
create all these links and monitor all conditions that may affect them so that
dynamic re-linking is possible in case something changes
(ex. if a device disconnects). In some cases, device and node configuration
may also need to change as a result of links being created or destroyed.

6. Metadata Management
^^^^^^^^^^^^^^^^^^^^^^

While in operation, PipeWire and WirePlumber both store some additional
properties about objects and their operation in storage that lives outside
these objects. These properties are referred to as "metadata" and they are
stored in "metadata objects". This metadata can be changed externally by tools
such as `pw-metadata`, but also others.

In some circumstances, this metadata needs to interact with logic inside
the session manager. Most notably, selecting the default audio and video inputs
and outputs is done by setting metadata. The session manager then needs to
validate this information, store it and restore it on the next restart, but also
ensure that the default inputs and outputs stay valid and reasonable when
devices are plugged and unplugged dynamically.
