.. _policies_software_dsp:

Automatic Software DSP
======================

Introduction
------------

WirePlumber provides a mechanism for transparently handling oddball and embedded
devices that require software DSP to be done in userspace. Devices such as smartphones,
TVs, portable speakers, and even some laptops implement an audio subsystem designed
under the assumption that the hardware sink/source will be "backed" by some sort
of transparent DSP mechanism. That is, the hardware device itself should not be
directly accessed, and expects to be sent preprocessed/pre-routed samples. Often,
especially with Android handsets, these samples are preprocessed or pre-routed
by the vendor's proprietary userspace.

WirePlumber's automatic software DSP mechanism aims to replicate this functionality in
a standardised and configurable way. The target device sink/source is hidden from
other PipeWire clients, and a virtual node is linked to it. This virtual
node is then presented to clients as *the* node, allowing implementors to specify
any custom processing or routing in a way that is transparent to users, the kernel,
and the hardware.


Activating
----------

In addition to the ``node.software-dsp.rules`` section, the ``node.software-dsp``
:ref:`feature <config_features>` must be enabled in the desired profile(s).


Matching a node
---------------

Matching rules are specified in ``node.software-dsp.rules``. The ``create-filter``
action specifies behaviour at node insertion. All node properties can be matched
on, including any type-specific properties such as ``alsa.id``.


Configurable properties
-----------------------

.. describe:: filter-graph

   SPA-JSON object describing the software DSP node. This is passed as-is as
   an argument to ``libpipewire-module-filter-chain``. See the
   `filter-chain documentation <https://docs.pipewire.org/page_module_filter_chain.html>`_
   for details on what options can be set in this object.

   .. note::

      The ``target.object`` property of the virtual node should be configured
      statically to point to the node matched by the rule.

.. describe:: filter-path

   Absolute path to a file on disk storing a SPA-JSON object as plain text. This will be
   parsed by WirePlumber into a WpConf object with a single section called
   ``node.software-dsp.graph``, then passed as-is into ``libpipewire-module-filter-chain``.

   .. note::

    ``filter-graph`` and ``filter-path`` are mutually exclusive, with the former taking
    precedence if both are present in the matched rule.

.. describe:: hide-parent

   Boolean indicating whether or not the matched node should be hidden from
   clients. ``node/software-dsp.lua`` will set the permissions for all clients other
   than WirePlumber itself to ``'-'``. This prevents use of the node by any
   userspace software except for WirePlumber itself.


Examples
--------

.. code-block::
   :caption: wireplumber.conf.d/99-my-dsp.conf

   node.software-dsp.rules = [
     {
       matches = [
         { "node.name" = "alsa_output.platform-sound.HiFi__Speaker__sink" }
         { "alsa.id" = "~WeirdHardware*" } # Wildcard match
       ]

       actions = {
         create-filter = {
           filter-graph = {} # Virtual node goes here
           filter-path = "/path/to/spa.json"
           hide-parent = true
         }
       }
     }
   ]

   wireplumber.profiles = [
     main = {
       node.software-dsp = required
     }
   ]


This will match any sinks with the UCM HiFi Speaker profile set that are associated
with cards containing the string "WeirdHardware" at the start of their name.
