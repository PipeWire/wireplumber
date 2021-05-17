Wireplumber
===========

WirePlumber is a modular session / policy manager for `PipeWire <https://pipewire.org>`_ and a GObject-based high-level library that wraps PipeWire's API, providing convenience for writing the daemon's modules as well as external tools for managing PipeWire.

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   installing-wireplumber.rst
   running-wireplumber-daemon.rst
   daemon-configuration.rst
   daemon-logging.rst
   contributing.rst
   community.rst
   testing.rst
   c_api.rst
   lua_api.rst

The WirePlumber Daemon
----------------------

The WirePlumber daemon implements the session & policy management service. It follows a modular design, having plugins that implement the actual management functionality.

* :ref:`running-wireplumber-daemon`
* :ref:`daemon-configuration`
* :ref:`logging`

The WirePlumber library
-----------------------

The WirePlumber Library provides API that allows you to extend the WirePlumber daemon, to write management or status tools for PipeWire (apps that don't do actual media streaming) and to write custom session managers for embedded devices.

Resources
---------

* :ref:`contributing`
* :ref:`testing`
* :ref:`community`

Subpages
--------

* :ref:`installing-wireplumber`
* :ref:`running-wireplumber-daemon`
* :ref:`daemon-configuration`
* :ref:`lua_api`
* :ref:`contributing`
* :ref:`testing`
* :ref:`community`

Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
