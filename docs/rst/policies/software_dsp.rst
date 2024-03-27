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
a standardised and configurable way. The target sink/source is hidden from
clients of the WirePlumber daemon, and a virtual node is linked to it. This virtual
node is then presented to clients as *the* node, allowing implementors to specify
any custom processing or routing in a way that is transparent to users, the kernel,
and the hardware.


Activating
----------

In addition to the ``node.software-dsp.rules`` section, the ``node.software-dsp``
component must be activated in the desired profile(s).


Matching a Node
--------------

Matching rules are specified in ``node.software-dsp.rules``. The ``create-filter``
action specifies behaviour at node insertion. All node properties can be matched
on, including any type-specific properties such as ``alsa.id``.


Configurable properties
-----------------------

- **filter-graph**

  SPA-JSON representing a virtual node. This is passed into
  ``libpipewire-module-filter-chain`` by ``node/software-dsp.lua``. This property is
  not recursed over - it is returned as a string. In a future
  release, ``filter-graph`` will instead specify the path to a file containing the
  SPA-JSON. The ``node.target`` property of the virtual node should point to
  the node matched by the rule.

- **hide-parent**

  Boolean indicating whether or not the matched node should be hidden from
  clients. ``node/software-dsp.lua`` will set the permissions for all clients other
  than WirePlumber itself to ``'-'``. This prevents use of the node by any
  userspace software except for WirePlumber itself.


Examples
--------

``wireplumber.conf.d/99-my-dsp.conf``
.. code-block::

  node.software-dsp.rules = [
    {
      matches = [
        { "node.name" = "alsa_output.platform-sound.HiFi__Speaker__sink" }
        { "alsa.id" = "~WeirdHardware*" } # Wildcard match
      ]

      actions = {
        create-filter = {
          filter-graph = {} # Virtual node goes here
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
