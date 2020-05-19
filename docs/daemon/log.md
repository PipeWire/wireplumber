# Debug Logging

Getting debug messages on the command line is a matter of setting the
`WIREPLUMBER_DEBUG` environment variable. The generic syntax is:
```
WIREPLUMBER_DEBUG=level:category1,category2,...
```

`level` can be a number from 1 to 5 and defines the minimum debug level to show:
 - 1 - warnings, critical warnings and fatal errors (`W`, `C` & `E` in the log)
 - 2 - normal messages (`M`)
 - 3 - informational messages (`I`)
 - 4 - debug messages (`D`)
 - 5 - trace messages (`T`)

`category1,category2,...` is an *optional* comma-separated list of debug
categories to show. Any categories not listed here will not appear in the log.
If no categories are specified, then all messages are printed.
Categories support [glob-style patterns](https://developer.gnome.org/glib/stable/glib-Glob-style-pattern-matching.html)
containing '*' and '?', for convenience.

Well known categories include:
  - `wireplumber`: messages from the wireplumber daemon
  - `pw`: messages from libpipewire & spa plugins
  - `wp-*`: messages from libwireplumber
    - `wp-core`: messages from `WpCore`
    - `wp-proxy`: messages from `WpProxy`
    - ... and so on ...
  - `m-*`: messages from wireplumber modules
    - `m-monitor`: messages from `libwireplumber-module-monitor`
    - `m-session-settings`: messages from `libwireplumber-module-session-settings`
    - ... and so on ...

## Examples

Show all messages
```
WIREPLUMBER_DEBUG=5
```

Show all messages up to the `debug` level (E, C, W, M, I & D), excluding `trace`
```
WIREPLUMBER_DEBUG=4
```

Show all messages up to the `message` level (E, C, W & M),
excluding `info`, `debug` & `trace`
(this is also the default when `WIREPLUMBER_DEBUG` is omitted)
```
WIREPLUMBER_DEBUG=2
```

Show all messages from the wireplumber library
```
WIREPLUMBER_DEBUG=5:wp-*
```

Show all messages from `wp-registry`, libpipewire and all modules
```
WIREPLUMBER_DEBUG=5:wp-registry,pw,m-*
```

## Relationship with the GLib log handler & G_MESSAGES_DEBUG

Older versions of WirePlumber used to use `G_MESSAGES_DEBUG` to control their
log output, which is the environment variable that affects GLib's default
log handler.

As of WirePlumber 0.3, `G_MESSAGES_DEBUG` is no longer used, since libwireplumber
replaces the default log handler.

If you are writing your own application based on libwireplumber, you can choose
if you want to replace this log handler using the flags passed to
[wp_init()](wp_init).

## Relationship with the PipeWire log handler & PIPEWIRE_DEBUG

libpipewire uses the `PIPEWIRE_DEBUG` environment variable, with a similar syntax.
WirePlumber replaces the log handler of libpipewire with its own, rendering
`PIPEWIRE_DEBUG` useless. Instead, you should use `WIREPLUMBER_DEBUG` and the
`pw` category to control log messages from libpipewire & its plugins.

If you are writing your own application based on libwireplumber, you can choose
if you want to replace this log handler using the flags passed to
[wp_init()](wp_init).
