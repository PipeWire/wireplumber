 .. _running-wireplumber-daemon:

Running the WirePlumber daemon
==============================

Replacing pipewire-media-session
--------------------------------

PipeWire 0.3 comes with an example session manager (pipewire-media-session)
that you will need to disable and replace with WirePlumber.

systemd
^^^^^^^

In most cases, ``pipewire-media-session`` is started by a systemd service unit,
``pipewire-media-session.service``.

To switch to WirePlumber, you will first need to disable that service:

.. code:: console

   $ systemctl --user --now disable pipewire-media-session

... and then, enable and use ``wireplumber.service`` in its place:

.. code:: console

   $ systemctl --user --now enable wireplumber

pipewire.conf
^^^^^^^^^^^^^

On some systems, ``pipewire-media-session`` is not started by systemd, but it
is started by pipewire itself via a configuration option in ``pipewire.conf``

To switch to wireplumber, you will need to edit
**/etc/pipewire/pipewire.conf** in an existing installation or
**src/daemon/pipewire.conf.in** in the PipeWire git tree
and change the appropriate line in the ``exec`` section:

.. code:: diff

   --- /etc/pipewire/pipewire.conf.bak
   +++ /etc/pipewire/pipewire.conf
   @@ -204,7 +204,7 @@ context.exec = [
        # but it is better to start it as a systemd service.
        # Run the session manager with -h for options.
        #
   -    #{ path = "/usr/bin/pipewire-media-session"  args = "" }
   +    { path = "wireplumber"  args = "" }
        #
        # You can optionally start the pulseaudio-server here as well
        # but it is better to start it as a systemd service.

.. code:: diff

   diff --git a/src/daemon/pipewire.conf.in b/src/daemon/pipewire.conf.in
   index bbafa134..16ef687b 100644
   --- a/src/daemon/pipewire.conf.in
   +++ b/src/daemon/pipewire.conf.in
   @@ -220,7 +220,7 @@ context.exec = [
        # but it is better to start it as a systemd service.
        # Run the session manager with -h for options.
        #
   -    @comment@{ path = "@media_session_path@"  args = "" }
   +    { path = "wireplumber"  args = "" }
        #
        # You can optionally start the pulseaudio-server here as well
        # but it is better to start it as a systemd service.

This setup assumes that WirePlumber is *installed* on the target system.

Run independently or without installing
---------------------------------------

If you wish to debug WirePlumber, it may be useful to run it separately from
PipeWire or run it directly from the source tree without installing.
To do so:

  1. Ensure that neither *WirePlumber* nor *pipewire-media-session*
     are running or started together with PipeWire

     - If any of those is started by systemd,

       - Stop the relevant systemd service, ``wireplumber.service``
         or ``pipewire-media-session.service``
       - Disable that service as well if you intend to restart PipeWire
         (so that the session manager is not restarted with it)

     - If any of those is started from pipewire.conf,

       - Kill it, in order to stop it temporarily: ``killall wireplumber``
         or ``killall pipewire-media-session``
       - Comment out with ``#`` the relevant ``{ path = "..."  args = "" }``
         line from the ``exec`` section in ``pipewire.conf``,
         if you intend to restart PipeWire

  2. Ensure that PipeWire is running

  3. Without stopping PipeWire, run WirePlumber.

     - if it is installed, execute ``wireplumber``
     - if it is **not** installed, execute ``make run`` in the source tree,
       or use the ``wp-uninstalled.sh`` script:

       .. code:: console

          $ ./wp-uninstalled.sh wireplumber
