.. _spa_device_api:

Spa Device
==========
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpSpaDevice;
   }

.. doxygenstruct:: WpSpaDevice
   :project: WirePlumber

.. doxygengroup:: wpspadevice
   :project: WirePlumber
   :content-only:
