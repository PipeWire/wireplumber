.. _si_interfaces_api:

Session Items Interfaces
========================
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GInterface -> WpSiEndpoint;
      GInterface -> WpSiAdapter;
      GInterface -> WpSiLinkable;
      GInterface -> WpSiLink;
      GInterface -> WpSiAcquisition;
   }

.. doxygenstruct:: WpSiEndpoint
   :project: WirePlumber

.. doxygenstruct:: _WpSiEndpointInterface
   :project: WirePlumber

.. doxygenstruct:: WpSiAdapter
   :project: WirePlumber

.. doxygenstruct:: _WpSiAdapterInterface
   :project: WirePlumber

.. doxygenstruct:: WpSiLinkable
   :project: WirePlumber

.. doxygenstruct:: _WpSiLinkableInterface
   :project: WirePlumber

.. doxygenstruct:: WpSiLink
   :project: WirePlumber

.. doxygenstruct:: _WpSiLinkInterface
   :project: WirePlumber

.. doxygenstruct:: WpSiAcquisition
   :project: WirePlumber

.. doxygenstruct:: _WpSiAcquisitionInterface
   :project: WirePlumber

.. doxygengroup:: wpsiinterfaces
   :project: WirePlumber
   :content-only:
