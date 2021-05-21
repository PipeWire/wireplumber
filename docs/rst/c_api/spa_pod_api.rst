.. _spa_pod_api:

Spa Pod (Plain Old Data)
========================
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GBoxed -> WpSpaPod;
      GBoxed -> WpSpaPodBuilder;
      GBoxed -> WpSpaPodParser;
   }

.. doxygenstruct:: WpSpaPod
   :project: WirePlumber

.. doxygenstruct:: WpSpaPodBuilder
   :project: WirePlumber

.. doxygenstruct:: WpSpaPodParser
   :project: WirePlumber

.. doxygengroup:: wpspapod
   :project: WirePlumber
   :content-only:
