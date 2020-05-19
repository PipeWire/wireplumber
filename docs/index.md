# WirePlumber

WirePlumber is a modular session / policy manager for
[PipeWire](https://pipewire.org) and a GObject-based high-level library
that wraps PipeWire's API, providing convenience for writing the daemon's
modules as well as external tools for managing PipeWire.

 * [Installing WirePlumber](installation/from-source.md)

## The WirePlumber Daemon

The WirePlumber daemon implements the session & policy management service.
It follows a modular design, having plugins that implement the actual
management functionality.

 * [Running the WirePlumber Daemon](daemon/running.md)
 * [Daemon Configuration](daemon/configuration.md)
 * [Debug Logging](daemon/log.md)

## The WirePlumber Library

The WirePlumber Library provides API that allows you
to extend the WirePlumber daemon, to write management or status tools
for PipeWire (apps that don't do actual media streaming)
and to write custom session managers for embedded devices.

 * [API Reference](gi-index)

## Resources

 * [Contribute to WirePlumber](contributing.md)
 * [Reach out to the community](community.md)
