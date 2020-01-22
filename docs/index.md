# WirePlumber

WirePlumber is a modular session / policy manager for
[PipeWire](https://pipewire.org) and a GObject-based high-level library
that wraps PipeWire's API, providing convenience for writing the daemon's
modules as well as external tools for managing PipeWire.

## Compiling

### Dependencies

In order to compile WirePlumber you will need:

* GLib >= 2.58
* PipeWire 0.3 (master)

At the moment, due to heavy development of both PipeWire and WirePlumber,
it is not always the case that the latest master of WirePlumber works with the
latest master of PipeWire. The safest PipeWire branch to use with WirePlumber
master is the `agl-next` branch from
[my personal clone](https://gitlab.freedesktop.org/gkiagia/pipewire)

### Compilation

WirePlumber uses the meson build system. For compatibility and ease of use,
though, a Makefile is also provided. The Makefile works only after the initial
configuration of the project with meson.

Here is the very basic sequence of compiling for the first time:
```
$ meson build
$ make
```

### Running automated tests

WirePlumber has a few automated tests that you can easily run with:

```
$ make test
```
