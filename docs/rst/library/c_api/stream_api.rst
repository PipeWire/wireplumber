.. _stream_api:

Streams
=======
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpStream;
      WpStream -> WpAudioStream;
      WpStream -> WpVideoStream;
   }

.. doxygenstruct:: WpStream

.. doxygenstruct:: WpAudioStream

.. doxygenstruct:: WpVideoStream

.. doxygengroup:: wpstream
   :content-only:
