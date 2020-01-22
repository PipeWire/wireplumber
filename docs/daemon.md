# Daemon

The WirePlumber daemon implements the session & policy management service.

## Running

1. First of all, you will need to run PipeWire. PipeWire itself comes with
an example session manager that you will need to disable in order to run
WirePlumber. This can be achieved by editing `src/daemon/pipewire.conf.in`
on the PipeWire git tree to disable the execution of the session manager:

```
diff --git a/src/daemon/pipewire.conf.in b/src/daemon/pipewire.conf.in
index bf64c574..e733e76c 100644
--- a/src/daemon/pipewire.conf.in
+++ b/src/daemon/pipewire.conf.in
@@ -24,4 +24,4 @@ load-module libpipewire-module-access
 load-module libpipewire-module-adapter
 load-module libpipewire-module-link-factory
 load-module libpipewire-module-session-manager
-exec build/src/examples/media-session
+#exec build/src/examples/media-session
```

2. Second, you will need to run pipewire: in the **pipewire** source tree, do `make run`

3. Without stopping pipewire, in the **wireplumber** source tree, do `make run`

## Testing with an audio client

The easiest way to test that things are working is to start a gstreamer pipeline
that outputs a test sound to pipewire.

In the **pipewire** source tree, do:

```
$ make shell
$ gst-launch-1.0 audiotestsrc ! pwaudiosink
```

Note that `pwaudiosink` is currently only available in the `agl-next` branch.

## Debugging

The Makefile included with WirePlumber also supports the `gdb` and `valgrind`
targets. So, instead of `make run` you can do `make gdb` or `make valgrind`
to do some debugging.

Getting debug messages on the command line is a matter of setting the
`G_MESSAGES_DEBUG` environment variable as documented in the GLib documentation.
Usually you can just do:

```
G_MESSAGES_DEBUG=all make run
```

Note that this only gives out WirePlumber's debug messages. If you want to also
see *libpipewire*'s debug messages, then you can also set:

```
PIPEWIRE_DEBUG=4 G_MESSAGES_DEBUG=all make run
```

... where `PIPEWIRE_DEBUG` can be set to a value between 1 and 5, with 5 being the
most verbose and 1 the least verbose.
