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

.. doxygenstruct:: WpSpaPodBuilder

.. doxygenstruct:: WpSpaPodParser

.. doxygengroup:: wpspapod
   :content-only:
