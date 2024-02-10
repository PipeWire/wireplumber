WirePlumber
===========

.. image:: https://gitlab.freedesktop.org/pipewire/wireplumber/badges/master/pipeline.svg
   :alt: Pipeline status

.. image:: https://scan.coverity.com/projects/21488/badge.svg
   :alt: Coverity Scan Build Status

.. image:: https://img.shields.io/badge/license-MIT-green
   :alt: License

.. image:: https://img.shields.io/badge/dynamic/json?color=informational&label=tag&query=%24%5B0%5D.name&url=https%3A%2F%2Fgitlab.freedesktop.org%2Fapi%2Fv4%2Fprojects%2F2941%2Frepository%2Ftags
   :alt: Tag

WirePlumber is a modular session / policy manager for
`PipeWire <https://pipewire.org>`_ and a GObject-based high-level library
that wraps PipeWire's API, providing convenience for writing the daemon's
modules as well as external tools for managing PipeWire.

The WirePlumber daemon implements the session & policy management service.
It follows a modular design, having plugins that implement the actual
management functionality.

The WirePlumber Library provides API that allows you to extend the WirePlumber
daemon, to write management or status tools for PipeWire
(apps that don't do actual media streaming) and to write custom session managers
for embedded devices.

Documentation
-------------

The latest version of the documentation is available online
`here <https://pipewire.pages.freedesktop.org/wireplumber/>`_
