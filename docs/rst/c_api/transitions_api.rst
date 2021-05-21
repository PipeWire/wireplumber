.. _transitions_api:

Transitions
===========
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpTransition;
      GInterface -> GAsyncResult;
      GAsyncResult -> WpTransition;
      WpTransition -> WpFeatureActivationTransition;
   }

.. doxygenstruct:: WpTransition
   :project: WirePlumber

.. doxygenstruct:: _WpTransitionClass
   :project: WirePlumber

.. doxygengroup:: wptransition
   :project: WirePlumber
   :content-only:

.. doxygenstruct:: WpFeatureActivationTransition
   :project: WirePlumber

.. doxygengroup:: wpfeatureactivationtransition
   :project: WirePlumber
   :content-only:
