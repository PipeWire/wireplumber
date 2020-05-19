# Running the WirePlumber Daemon

## Configure PipeWire

PipeWire 0.3 comes with an example session manager that you will need
to disable and replace with WirePlumber. This can be achieved by editing
`src/daemon/pipewire.conf.in` in the PipeWire git tree or
`/etc/pipewire/pipewire.conf` in an existing installation:

```
diff --git a/src/daemon/pipewire.conf.in b/src/daemon/pipewire.conf.in
index b659d460..93299ec2 100644
--- a/src/daemon/pipewire.conf.in
+++ b/src/daemon/pipewire.conf.in
@@ -73,4 +73,4 @@ create-object spa-node-factory factory.name=support.node.driver node.name=Dummy
 # Execute the given program. This is usually used to start the
 # session manager. run the session manager with -h for options
 #
-exec pipewire-media-session # -d alsa-seq,alsa-pcm,bluez5,metadata
+exec wireplumber
```

This setup assumes that WirePlumber is *installed* on the target system.
If you wish

## Run independently or without installing

If you wish to debug WirePlumber, it may be useful to run it separately from
PipeWire or run it directly from the source tree without installing.
To do so:

1. Comment out with `#` the `exec` line from `pipewire.conf`
2. Run pipewire:
  - if it is installed, execute `pipewire`
  - if it is **not** installed, execute `make run` in the **pipewire** source tree
3. Without stopping pipewire, run wireplumber:
  - if it is installed, execute `wireplumber`
  - if it is **not** installed, execute `make run` in the **wireplumber** source tree
