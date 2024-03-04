.. _metadata_api:

PipeWire Metadata
=================
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GBoxed -> WpMetadataItem
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpMetadata;
      WpMetadata-> WpImplMetadata;
   }

.. doxygenstruct:: WpMetadataItem

.. doxygenstruct:: WpMetadata

.. doxygenstruct:: WpImplMetadata

.. doxygengroup:: wpmetadata
   :content-only:
