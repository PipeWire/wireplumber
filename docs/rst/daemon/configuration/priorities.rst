.. _config_priorities:

Node priorities
===============

Unless the user has chosen a default sink or source, WirePlumber makes the
available node with the highest ``priority.session`` the default. Raising or
lowering it is how one device is preferred over another.

.. important::

   Priorities only decide the default node as long as there is no saved
   user selection. A node that was selected with ``wpctl set-default`` — or
   by any other client that sets the default, such as a desktop volume
   applet — is remembered in the ``default-nodes``
   :ref:`state file <state_locations>` and outranks every
   ``priority.session`` value on all subsequent starts, which makes changes
   to this property appear to have no effect at all. The saved selections
   are listed in the "Default Configured Devices" section of
   ``wpctl status``; ``wpctl clear-default`` forgets them and lets the
   priorities decide again. To stop selections from being remembered at all,
   disable ``node.restore-default-targets`` (see :ref:`config_settings`).

Setting the priority
--------------------

``priority.session`` is set in the rules of the monitor that creates the node,
so ALSA and Bluetooth devices need separate rules, even though they compete
with each other. For example, to prefer USB sound cards and to keep Bluetooth
headphones from becoming the default when they connect:

Example configuration :ref:`fragment <config_conf_file_fragments>` file:

.. code-block::

   monitor.alsa.rules = [
     {
       matches = [
         {
           node.name = "~alsa_output.usb-.*"
         }
       ]
       actions = {
         update-props = {
           priority.session = 3000
         }
       }
     }
   ]

   monitor.bluez.rules = [
     {
       matches = [
         {
           node.name = "~bluez_output.*"
         }
       ]
       actions = {
         update-props = {
           priority.session = 900
         }
       }
     }
   ]

To list ``priority.session``, ``node.nick`` and ``node.name`` of every sink, you can run:

.. code-block:: bash

   for kind in "audio sinks" "audio sources" "video sources"; do
     echo "$kind:"
     for id in $(wpctl list $kind | cut -f1); do
       wpctl inspect "$id" | awk -F' = ' '
         $1 ~ / priority\.session$/ { p = $2 }
         $1 ~ / node\.nick$/ { n = $2 }
         $1 ~ / node\.name$/ { m = $2 }
         END { print p, n, m }'
     done | sort -t'"' -k2rn
   done

..

Hardware determines which property identifies a node reliably. ``node.name``
is usually a good choice but ``node.nick`` or for example ``api.bluez5.address``
can be useful too.

Cameras are set in ``monitor.v4l2.rules`` or ``monitor.libcamera.rules`` (see
:ref:`config_video`).

Priority ranking
--------------------
Default sinks, sources and cameras are chosen separately, so a node only
competes with nodes of the same kind.

Nodes that tie on ``priority.session`` are ranked further by the
priority of the route they play on, but only against nodes of the same
card: route priorities rank the outputs of one card against each other
and are not comparable across cards. Whatever tie is left is decided in
favour of the object that appeared first, so that the default node does
not change on its own while the system is running.

Two cards tying on ``priority.session`` means that no preference between
them has been expressed. Which of them appeared first follows the order
in which the PipeWire monitor discovered them, which is stable while the
system runs but is not guaranteed across a restart, so do not rely on
it: set ``priority.session`` on one of them to state the preference.

.. note::

   Audio nodes also have ``priority.driver``. It does not affect the default
   and does not normally need to be set; see `pipewire-props(7)`.

.. _pipewire-props(7): https://docs.pipewire.org/page_man_pipewire-props_7.html
