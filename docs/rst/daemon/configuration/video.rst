.. _config_video:

Video & camera configuration
============================

WirePlumber can expose cameras through two different backends: the **V4L2**
monitor, which talks to the kernel's Video4Linux2 interface directly, and the
**libcamera** monitor, which uses libcamera and is able to drive more complex
camera pipelines (notably the ISP-based cameras found on many ARM platforms).

Both monitors are enabled by default. They are grouped under the
``hardware.video-capture`` feature, which in turn wants ``monitor.v4l2`` and
``monitor.libcamera``; see :ref:`config_features`. The ``video-capture``
profile brings up only the video part of WirePlumber, which is useful when
running :ref:`multiple instances <daemon_multi_instance>`.

.. note::

   Cameras are also subject to access control. Under a sandbox such as Flatpak
   or Snap, an application only sees camera nodes if it has been granted
   permission; see :ref:`config_access`.

Choosing a backend
------------------

Because both monitors enumerate the same hardware, the same physical camera is
often reported twice. WirePlumber arbitrates between them rather than exposing
duplicates, using the device numbers each backend reports in the
``device.devids`` property:

1. V4L2 devices driven by ``uvcvideo`` (ordinary USB webcams) are created
   first — for these, V4L2 is preferred.
2. libcamera devices are created next, unless their device numbers were
   already claimed by a V4L2 UVC device.
3. Remaining V4L2 devices are created last, unless libcamera already claimed
   them.

The arbitration needs both monitors to have reported, so node creation is
delayed by the ``monitor.camera-discovery-timeout`` setting (1 second by
default); see :ref:`config_settings`. A device that reports no device numbers
at all bypasses the arbitration and is created immediately.

To use libcamera exclusively, disable the V4L2 monitor in your profile:

.. code-block::

   wireplumber.profiles = {
     main = {
       monitor.v4l2 = disabled
     }
   }

Conversely, set ``monitor.libcamera = disabled`` to use V4L2 only.

V4L2 configuration
------------------

.. describe:: monitor.v4l2.properties

   The properties used when constructing the ``api.v4l2.enum.udev`` SPA plugin,
   which does the actual device enumeration.

.. describe:: monitor.v4l2.rules

   Rules that are matched against V4L2 devices and nodes as they are created,
   allowing their properties to be modified. The syntax is the same as for the
   other monitor rules; see :ref:`config_modifying_configuration`.

   Device rules are matched against device properties such as ``device.name``:

   .. code-block::

      monitor.v4l2.rules = [
        {
          matches = [
            {
              device.name = "~v4l2_device.*"
            }
          ]
          actions = {
            update-props = {
              device.nick = "My Device"
              device.disabled = false
            }
          }
        }
      ]

   Node rules are matched against node properties such as ``node.name``:

   .. code-block::

      monitor.v4l2.rules = [
        {
          matches = [
            {
              node.name = "~v4l2_input.*"
            }
          ]
          actions = {
            update-props = {
              node.nick          = "My Node"
              priority.session   = 100
              node.pause-on-idle = false
              node.disabled      = false
            }
          }
        }
      ]

   Setting ``device.disabled`` or ``node.disabled`` to ``true`` is how a
   specific camera is hidden from the graph.

libcamera configuration
-----------------------

.. describe:: monitor.libcamera.properties

   The properties used when constructing the ``api.libcamera.enum.manager``
   SPA plugin.

.. describe:: monitor.libcamera.rules

   The same as ``monitor.v4l2.rules``, but for devices and nodes created by the
   libcamera monitor. Device names match ``libcamera_device.*`` and node names
   match ``libcamera_input.*``.

Examples
--------

Ready-made, commented example fragments for both backends ship with
WirePlumber as ``v4l2.conf`` and ``libcamera.conf``; see
:ref:`config_example_fragments`.

Troubleshooting
---------------

**My camera does not appear at all.**
Check that ``hardware.video-capture`` is enabled in your profile and that the
relevant monitor is not disabled. Then check the logs with
``WIREPLUMBER_DEBUG=s-monitors*:D``, which shows what each monitor enumerated
and which devices were skipped by the arbitration described above.

**My camera appears but produces no image.**
This is usually a backend issue rather than a WirePlumber one. Try the other
backend; ``libcamera`` is often required for cameras that need ISP processing,
while plain UVC webcams generally work better with V4L2.
