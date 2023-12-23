.. _resources_testing:

Testing
=======

Automated unit tests
--------------------

WirePlumber has automated tests that you can easily run with:

.. code:: console

   $ meson test -C build

This will automatically compile all test dependencies, so you can be sure
that this always tests your latest changes.

If you wish to run a specific test instead of all of them, you can run:

.. code:: console

   $ meson test -C build test-name

When debugging a single test, you can additionally enable verbose test output
by appending ``-v`` and you can also run the test in gdb by appending ``--gdb``.

For more information on how to use ``meson test``, please refer to
`meson's manual <https://mesonbuild.com/Unit-tests.html>`_

.. important::

   When submitting changes for review, always ensure that all tests pass

Please note that many WirePlumber tests require specific SPA test plugins
to be available in your PipeWire installation. More specifically, PipeWire
needs to be configured with the following options enabled:

.. code:: console

   -Dvideotestsrc=true -Daudiotestsrc=true -Dtest=true

If these SPA plugins are not found in the system, some tests will fail.
This is expected.

WirePlumber examples
--------------------

WirePlumber ships examples in ``test/examples``.
Execute them from the top-level directory with ``wp-uninstalled.sh``:

.. code:: console

   $ ./wp-uninstalled.sh ./build/tests/examples/audiotestsrc-play


Assuming there is no other process actively using ``hw:0,0`` from alsa,
the above example should play a test tone on ``hw:0,0`` without errors.

Native API clients
------------------

pw-cat
^^^^^^

Using the default device:

.. code:: console

   $ wpctl status  # verify the default device
   $ pw-record test.wav
   $ pw-play test.wav


Using a non-default device:

.. code:: console

   $ pw-record --list-targets  # find the node id
   $ pw-record --target <node_id> test.wav
   $ pw-play --list-targets  # find the node id
   $ pw-play --target <node_id> test.wav

or

.. code:: console

   $ wpctl status  # find the capture & playback node ids
   $ pw-record --target <node_id> test.wav
   $ pw-play --target <node_id> test.wav

.. note::

   node ids can be used interchangeably when specifying targets in all use cases

video-play
^^^^^^^^^^

Using the default device:

.. code:: console

   $ cd path/to/pipewire-source-dir
   $ ./build/src/examples/video-play


Using a non-default device:

.. code:: console

   $ wpctl status  # find the device node id from the list
   $ cd path/to/pipewire-source-dir
   $ ./build/src/examples/video-play <node_id>

.. tip::

   enable videotestsrc in wireplumber's configuration to have more video
   sources available

PulseAudio compat API clients
-----------------------------

pacat
^^^^^

Using the default device:

.. code:: console

   $ wpctl status  # verify the default device
   $ parecord test.wav
   $ paplay test.wav

pavucontrol
^^^^^^^^^^^

Use the command:

.. code:: console

  $ pavucontrol

* Volume level meters should work
* Changing the volume should work

ALSA compat API clients
-----------------------

aplay / arecord
^^^^^^^^^^^^^^^

.. note::

   unless you have installed PipeWire in the default system prefix
   (``/usr``), the ALSA compat API will not work, unless you copy
   ``libasound_module_pcm_pipewire.so`` in the alsa plugins directory
   (usually ``/usr/<libdir>/alsa-lib/``) and that you add the contents of
   ``pipewire-alsa/conf/50-pipewire.conf`` in your ``~/.asoundrc``
   (or anywhere else, system-wide, where libasound can read it)

Using the default device:

.. code:: console

   $ wpctl status  # verify the default devices
   $ arecord -D pipewire -f S16_LE -r 48000 test.wav
   $ aplay -D pipewire test.wav

Using a non-default device:

.. code:: console

   $ wpctl status  # find the capture & playback node ids
   $ PIPEWIRE_NODE=<node_id> arecord -D pipewire -f S16_LE -r 48000 test.wav
   $ PIPEWIRE_NODE=<node_id> aplay -D pipewire test.wav

or

.. code:: console

   $ wpctl status  # find the capture & playback device node ids
   $ arecord -D pipewire:NODE=<node_id> -f S16_LE -r 48000 test.wav
   $ aplay -D pipewire:NODE=<node_id> test.wav


JACK compat API clients
-----------------------

qjackctl
^^^^^^^^

.. code:: console

   $ pw-jack qjackctl

* This should correctly connect.
* The "Graph" window should show the PipeWire graph.

jack_simple_client
^^^^^^^^^^^^^^^^^^

.. code:: console

   $ wpctl status  # find the target device node id
   $ wpctl inspect <node_id>  # find the node.id
   $ PIPEWIRE_NODE=<node_id> pw-jack jack_simple_client

.. note::

   The JACK layer is not controlled by the session manager, it creates its own
   links; which is why it is required to specify a node id.

Device Reservation
------------------

with PulseAudio
^^^^^^^^^^^^^^^

1. With PulseAudio running, start a pulseaudio client.

.. code:: console

   $ gst-launch-1.0 audiotestsrc ! pulsesink

2. Start PipeWire & WirePlumber

   - The device in use by PA will not be available in PW

3. Stop the PA client

   - A few seconds later, WirePlumber should assume control of the device

4. ``wpctl status`` should be able to confirm that the device is available

5. Start a PA client again

   - It should not be able to play; it will just freeze

6. Stop WirePlumber

   - The PA client should immediately start playing

with JACK
^^^^^^^^^

1. Start PipeWire & WirePlumber

   - All devices should be available

2. Start ``jackdbus``

   1. through ``qjackctl``:

      - Enable *Setup* -> *Misc* -> *Enable JACK D-Bus interface*
      - Click *Start* on the main window

   2. or manually:

      - Run ``jackdbus auto``
      - Run ``qdbus org.jackaudio.service /org/jackaudio/Controller org.jackaudio.JackControl.StartServer``

3. Wait a few seconds and run ``wpctl status`` to inspect

   - The devices taken by JACK should no longer be available
   - There should be two *JACK System* nodes (sink & source)

4. Run an audio client on PipeWire (ex ``pw-play test.wav``)

   - Notice how audio now goes through JACK

5. Stop JACK

   - through ``qjackctl``, click *Stop*
   - or manually: ``qdbus org.jackaudio.service /org/jackaudio/Controller org.jackaudio.JackControl.StopServer``

6. Wait a few seconds and run ``wpctl status`` to inspect

   - The devices that were release by JACK should again be available
   - There should be no *JACK System* nodes

.. note::

   You may also start WirePlumber *after* starting JACK. It should immediately
   go to the state described in step 3
