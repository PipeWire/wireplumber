.. _daemon_running:

Running the WirePlumber daemon
==============================

Systemd
-------

WirePlumber comes with a systemd unit, ``wireplumber.service``, which should
be enabled on your user session:

.. code:: console

   $ systemctl --user --now enable wireplumber

.. note::

   On non-systemd systems, you just need to ensure that wireplumber is started
   after pipewire.

Run from the PipeWire source tree
---------------------------------

PipeWire's build system comes with an option to build WirePlumber together
with PipeWire and allows executing them together without installing either of
them.

To make this work, configure PipeWire with the
``-Dsession-managers="[ 'wireplumber' ]"`` option on the meson command line.

When compiling PipeWire, the build system will now also clone and compile
WirePlumber as a subproject.

To execute the whole stack without installing, simply execute ``make run``
after compiling.

Synopsis:

.. code:: console

   $ meson -Dsession-managers="[ 'wireplumber' ]" build
   $ ninja -C build
   $ make run

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
         line from the ``context.exec`` section in ``pipewire.conf``,
         if you intend to restart PipeWire

  2. Ensure that PipeWire is running

  3. Without stopping PipeWire, run WirePlumber.

     - if it is installed, execute ``wireplumber``
     - if it is **not** installed, execute ``make run`` in the source tree,
       or use the ``wp-uninstalled.sh`` script:

       .. code:: console

          $ ./wp-uninstalled.sh wireplumber

Replacing pipewire-media-session
--------------------------------

Older versions of PipeWire used to be distributed with an example session
manager (pipewire-media-session) that you needed to disable and replace with
WirePlumber.

.. warning::

  These instructions are only relevant to older versions of PipeWire

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
