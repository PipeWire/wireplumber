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

.. doxygenstruct:: _WpTransitionClass

.. doxygengroup:: wptransition
   :content-only:

.. doxygenstruct:: WpFeatureActivationTransition

.. doxygengroup:: wpfeatureactivationtransition
   :content-only:
