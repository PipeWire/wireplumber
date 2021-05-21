.. _session_item_api:

Session Items
=============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpSessionItem;
   }

.. doxygenstruct:: WpSessionItem
   :project: WirePlumber

.. doxygenstruct:: _WpSessionItemClass
   :project: WirePlumber

.. doxygengroup:: wpsessionitem
   :project: WirePlumber
   :content-only:
