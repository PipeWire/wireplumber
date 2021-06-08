.. _device_api:

PipeWire Device
===============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpDevice;
      GInterface -> WpPipewireObject;
      WpPipewireObject -> WpDevice;
   }

.. doxygenstruct:: WpDevice

.. doxygengroup:: wpdevice
   :content-only:
