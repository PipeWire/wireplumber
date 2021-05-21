.. _metadata_api:

PipeWire Metadata
=================
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpMetadata;
      WpMetadata-> WpImplMetadata;
   }

.. doxygenstruct:: WpMetadata
   :project: WirePlumber

.. doxygenstruct:: WpImplMetadata
   :project: WirePlumber

.. doxygengroup:: wpmetadata
   :project: WirePlumber
   :content-only:
