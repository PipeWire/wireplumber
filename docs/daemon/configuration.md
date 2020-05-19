# Configuration

WirePlumber is a heavily modular daemon. By itself, it doesn't do anything
except load the configured modules. All the rest of the logic is implemented
inside those modules.

Modular design ensures that it is possible to swap the implementation of
specific functionality without having to re-implement the rest of it, allowing
flexibility on target-sensitive parts, such as policy management and
making use of non-standard hardware.

## `wireplumber.conf`

This is WirePlumber's main configuration file. It is read at startup, before
connecting to the PipeWire daemon. Its purpose is to list all the modules
that need to be loaded by WirePlumber.

The format of this file is custom and resembles a script with commands:

```
# comment
command parameter1 parameter2 ...
```

Lines are executed in the order they appear and each of them executes an
action defined by the command. Lines starting with `#` are treated as comments
and ignored. Possible commands are:

* `add-spa-lib`

  Associates SPA plugin names with the names of the SPA modules that they
  can be loaded from. This takes 2 parameters: a name pattern and a library name.

  This actually does not load the SPA plugin, it only calls `pw_core_add_spa_lib`
  with the 2 paramteres given as arguments. As a consequence, it is safe to
  call this even if the SPA module is not actually installed on the system.

  Example:
  ```
  add-spa-lib api.alsa.* alsa/libspa-alsa
  ```

  In this example, we let `libpipewire` know that any SPA plugin whose name
  starts with `api.alsa.` can be loaded from the SPA module
  `alsa/libspa-alsa.so` (relative to the standard SPA modules directory).

* `load-pipewire-module`

  Loads a `libpipewire` module. This is similar to the `load-module` commands
  that would appear on `pipewire.conf`, the configuration file of the PipeWire
  daemon.

  This takes at least 1 parameter, the module name, and optionally any module
  arguments, in the format that they would be given in `pipewire.conf`

  Format:
  ```
  load-pipewire-module module-name some-argument some-property=value
  ```
  Example:
  ```
  load-pipewire-module libpipewire-module-client-device
  ```

  This command does not affect the PipeWire daemon by any means. It exists
  simply to allow loading `libpipewire` modules in the pipewire core that
  runs inside WirePlumber. This is usually useful to load pipewire protocol
  extensions, so that you can export custom objects to PipeWire and other
  clients.

* `load-module`

  Loads a WirePlumber module. This takes 2 arguments and an optional parameter
  block.

  Format:
  ```
  load-module ABI module-name {
    "parameter": <"value">
  }
  ```

  The `ABI` parameter specifies the binary interface that WirePlumber shall use
  to load this module. Currently, the only supported ABI is `C`. It exists to
  allow future expansion, writing modules in other languages.

  The `module-name` should be the name of the `.so` file without the `.so`
  extension.

  Optionally, if the `load-module` line ends with a `{`, the next lines up to
  and including the next matching `}` are treated as a parameter block.
  This block essentially is a
  [GVariant](https://developer.gnome.org/glib/stable/glib-GVariant.html)
  of type
  [`a{sv}`](https://developer.gnome.org/glib/stable/gvariant-format-strings.html)
  in the
  [GVariant Text Format](https://developer.gnome.org/glib/stable/gvariant-text.html).
  As a rule of thumb, parameter names in this block must always be strings
  enclosed in double quotes, the separation between names and values is done
  with the `:` character and values, regardless of their inner type, must always
  be enclosed in `<` `>`.

  Note that starting the parameter block on the next line is an error. The
  starting brace (`{`) must always be on the `load-module` line.

  Example:
  ```
  load-module C libwireplumber-module-monitor {
    "factory": <"api.alsa.enum.udev">,
    "flags": <["use-adapter", "activate-devices"]>
  }
  ```

  Parameters are module-dependent. They are passed as a GVariant in the
  module's initialization function and it is up to the module to interpret
  their meaning. WirePlumber does not have any reserved parameters.

## Location of configuration files

WirePlumber's default location of its configuration files is determined at
compile time by the build system. Typically, it ends up being `/etc/wireplumber`.

In more detail, this is controlled by the `--sysconfdir` meson option. When
this is set to an absolute path, such as `/etc`, the location of the
configuration files is set to be `$sysconfdir/wireplumber`. When this is set
to a relative path, such as `etc`, then the installation prefix (`--prefix`)
is prepended to the path: `$prefix/$sysconfdir/wireplumber`

WirePlumber expects its `wireplumber.conf` to reside in that directory.
It is possible to override that at runtime by setting the
`WIREPLUMBER_CONFIG_FILE` environment variable:

```
WIREPLUMBER_CONFIG_FILE=src/config/wireplumber.conf wireplumber
```

It is also possible to override the whole configuration directory, so that
all other configuration files are being read from a different location as well,
by setting the `WIREPLUMBER_CONFIG_DIR` environment variable:
```
WIREPLUMBER_CONFIG_DIR=src/config wireplumber
```

## Location of modules

### WirePlumber modules

Like with configuration files, WirePlumber's default location of its modules is
determined at compile time by the build system. Typically, it ends up being
`/usr/lib/wireplumber-0.1` (or `/usr/lib/<arch-triplet>/wireplumber-0.1` on
multiarch systems)

In more detail, this is controlled by the `--libdir` meson option. When
this is set to an absolute path, such as `/lib`, the location of the
modules is set to be `$libdir/wireplumber-$abi_version`. When this is set
to a relative path, such as `lib`, then the installation prefix (`--prefix`)
is prepended to the path: `$prefix/$libdir/wireplumber-$abi_version`.

It is possible to override this directory at runtime by setting the
`WIREPLUMBER_MODULE_DIR` environment variable:
```
WIREPLUMBER_MODULE_DIR=build/modules wireplumber
```

### PipeWire and SPA modules

PipeWire and SPA modules are not loaded from the same location as WirePlumber's
modules. They are loaded from the location that PipeWire loads them.

It is also possible to override these locations by using environment variables:
`SPA_PLUGIN_DIR` and `PIPEWIRE_MODULE_DIR`. For more details, refer to
PipeWire's documentation.

# module-monitor

This module internally loads a SPA "device" object which enumerates all the
devices of a certain subsystem. Then it listens for "node" objects that are
being created by this device and exports them to PipeWire, after adjusting
their properties to provide enough context.

`module-monitor` does not read any configuration files, however, it supports
configuration through parameters defined in the main `wireplumber.conf`.
Possible parameters are:

* `factory`

  A string that specifies the name of the SPA factory that loads the intial
  "device" object.

  Well-known factories are:

  * "api.alsa.enum.udev" - Discovers ALSA devices via udev
  * "api.v4l2.enum.udev" - Discovers V4L2 devices via udev
  * "api.bluez5.enum.dbus" - Discovers bluetooth devices by calling bluez5 API via D-Bus

 * `flags`

    An array of strings that enable specific functionality in the monitor.
    Possible flags include:

    * "use-adapter"

      Instructs the monitor to wrap all the created nodes in an "adapter"
      SPA node, which provides automatic port splitting/merging and format/rate
      conversion. This should be always enabled for audio device nodes.

    * "local-nodes"

      Instructs the monitor to run all the created nodes locally in in the
      WirePlumber process, instead of the default behavior which is to create
      the nodes in the PipeWire process. This is useful for bluetooth nodes,
      which should run outside of the main PipeWire process for performance
      reasons.

    * "activate-devices"

      Instructs the monitor to automatically set the device profile to "On",
      so that the nodes are created. If not specified, the profile must be
      set externally by the user before any nodes appear.

# module-config-endpoint

This module creates endpoints when WirePlumber detects new nodes in the
pipewire graph. Nodes themselves can be created in two ways:
Device modes are being created by "monitors" that watch a specific subsystem
(udev, bluez, etc...) for devices. Client nodes are being created by client
applications that try to stream to/from pipewire. As soon as a node is created,
the `module-config-endpoint` iterates through all the `.endpoint` configuration
files, in the order that is determined by the filename, and tries to match the
node to the node description in the `[match-node]` table. Upon a successful
match, a new endpoint that follows the description in the `[endpoint]` table is
created.

## `*.endpoint` configuration files

These files are TOML v0.5 files. At the top-level, they must contain exactly
2 tables: `[match-node]` and `[endpoint]`

The `[match-node]` table contains properties that match a pipewire node that
exists on the graph. Possible fields of this table are:

* `properties`

  This is a TOML array of tables, where each table must contain two fields:
  `name` and `value`, both being strings. Each table describes a match against
  one of the pipewire properties of the node. For a successful node match, all
  the described properties must match with the node.

  The value of the `name` field must match exactly the name of the pipewire
  property, while the value of the `value` field can contain '*' (wildcard)
  and '?' (joker), adhering to the rules of the
  [GLib g_pattern_match() function](https://developer.gnome.org/glib/stable/glib-Glob-style-pattern-matching.html).

  When writing `.endpoint` files, a useful utility that you can use to list
  device node properties is:

  ```
  $ wireplumber-cli device-node-props
  ```

  Another way to figure out some of these properties *for ALSA nodes* is
  by parsing the aplay/arecord output. For example, this line from `aplay -l`
  is interpreted as follows:

  ```
  card 0: PCH [HDA Intel PCH], device 2: ALC3246 [ALC3246 Analog]
  ```

  ```
  { name = "api.alsa.path", value = "hw:0,2" },
  { name = "api.alsa.card", value = "0" },
  { name = "api.alsa.card.id", value = "PCH" },
  { name = "api.alsa.card.name", value = "HDA Intel PCH" },
  { name = "api.alsa.pcm.device", value = "2" },
  { name = "api.alsa.pcm.id", value = "ALC3246" },
  { name = "api.alsa.pcm.name", value = "ALC3246 Analog" },
  ```

The `[endpoint]` table contains a description of the endpoint to be created.
Possible fields of this table are:

* `session`

  Required. A String representing the session name to be used when exporting the
  endpoint.

* `type`

  Required. Specifies the factory to be used for construction.
  The only well-known factories at the moment of writing is: `si-adapter` and
  `si-simple-node-edpoint`.

* `streams`

  Optional. Specifies the name of a `.streams` file that contains the
  descriptions of the streams to create for this endpoint. This currently
  specific to the implementation of the `pw-audio-softdsp-endpoint` and might
  change in the future.

* `config`

  Optional. Specifies the configuration table used to configure the endpoint.
  This table can have the following entries:

    * `name`

      Optional. The name of the newly created endpoint. If not specified, the
      endpoint is named after the node (from the `node.name` property of the
      node).

    * `media_class`

      Optional. A string that specifies an override for the `media.class`
      property of the node. It can be used in special circumstances to declare
      that an endpoint is dealing with a different type of data. This is only
      useful in combination with a policy implementation that is aware of this
      media class.

    * `role`

      Optional. A string representing the role of the endpoint.

    * `priority`

      Optional. An unsigned integer that specifies the order in which endpoints
      are chosen by the policy.

      If not specified, the default priority of an endpoint is equal to zero
      (i.e. the lowest priority).

    * `enable-control-port`

      Optional. A boolean representing whether the control port should be
      enabled on the endpoint or not.

    * `enable-monitor`

      Optional. A boolean representing whether the monitor ports should be
      enabled on the endpoint or not. 

## `*.streams` configuration files

These files contain lists of streams with their names and priorities.
They are TOML v0.5 files.

Each `.streams` file must contain exactly one top-level array of tables,
called `streams`. Every table must contain a mandatory `name` field, and 2
optional fields: `priority` and `enable_control_port`.

The `name` of each stream is used to create the streams on new endpoints.

The `priority` of each stream is being interpreted by the policy module to
apply restrictions on which app can use the stream at a given time.

The `enable_control_port` is used to enable the control port of the stream.

# module-config-policy

This module implements demo-quality policy management that is partly driven
by configuration files. The configuration files that this module reads are
described below:

## `*.endpoint-link`

These files contain rules to link endpoints with each other.
They are TOML v0.5 files.

Endpoints are normally created by another module, such
as `module-config-endpoint` which is described above.
As soon as an endpoint is created, the `module-config-policy` uses the
information gathered from the `.endpoint-link` files in order to create a
link to another endpoint.

`.endpoint-link` files can contain 3 top-level tables:
* `[match-endpoint]`, required
* `[target-endpoint]`, optional

The `[match-endpoint]` table contains properties that match an endpoint that
exists on the graph. Possible fields of this table are:

* `name`

  Optional. The name of the endpoint. It is possible to use wildcards here to
  match only parts of the name.

* `media_class`

  Optional. A string that specifies the `media.class` that the endpoint
  must have in order to match.

* `properties`

  This is a TOML array of tables, where each table must contain two fields:
  `name` and `value`, both being strings. Each table describes a match against
  one of the pipewire properties of the endpoint. For a successful endpoint
  match, all the described properties must match with the endpoint.

The `[target-endpoint]` table contains properties that match an endpoint that
exists on the graph. The purpose of this table is to match a second endpoint
that the original matching endpoint from `[match-endpoint]` will be linked to.
If not specified, `module-config-policy` will look for the session "default"
endpoint for the type of media that the matching endpoint produces or consumes
and will use that as a target. Possible fields of this table are:

* `name`, `media_class`, `properties`

  All these fields are permitted and behave exactly as described above for the
  `[match-endpoint]` table.

* `stream`

  This field specifies a stream name that the link will use on the target
  endpoint. If it is not specified, the stream name is acquired from the
  `media.role` property of the matching endpoint. If specified, the value of
  this field overrides the `media.role`.
