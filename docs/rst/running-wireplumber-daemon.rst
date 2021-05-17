 .. _running-wireplumber-daemon:

Running the WirePlumber daemon
==============================

Configure PipeWire
------------------

PipeWire 0.3 comes with an example session manager that you will need
to disable and replace with WirePlumber. This can be achieved by editing
**src/daemon/pipewire.conf.in** in the PipeWire git tree or
**/etc/pipewire/pipewire.conf** in an existing installation:

Here, is the set of changes required to disable the default session manager and
to replace it with WirePlumber::

  diff --git a/src/daemon/pipewire.conf.in b/src/daemon/pipewire.conf.in
  index cebded96..dee1743b 100644
  --- a/src/daemon/pipewire.conf.in
  +++ b/src/daemon/pipewire.conf.in
  @@ -99,7 +99,8 @@ exec = {
       # Start the session manager. Run the session manager with -h for
       # options.
       #
  -    "@media_session_path@" = { args = ""}
  +    #"@media_session_path@" = { args = ""}
  +    "wireplumber" = {}
       #
       # You can optionally start the pulseaudio-server here as well
       # but it better to start it as a systemd service.

This setup assumes that WirePlumber is *installed* on the target system.

Run independently or without installing
---------------------------------------

If you wish to debug WirePlumber, it may be useful to run it separately from
PipeWire or run it directly from the source tree without installing.
To do so -

1. Comment out with *#* the *"wireplumber" = {}* line from *pipewire.conf*

2. Run pipewire.

  * if it is installed, execute *pipewire*
  * if it is **not** installed, execute *make run* in the **pipewire** source tree

3. Without stopping pipewire, run wireplumber.

  * if it is installed, execute *wireplumber*
  * if it is **not** installed, execute *make run* in the **wireplumber** source tree
