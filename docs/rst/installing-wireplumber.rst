 .. _installing-wireplumber:

Installing WirePlumber
======================

Dependencies
------------

In order to compile WirePlumber you will need:

* GLib >= 2.58
* PipeWire 0.3 (>= 0.3.26)
* Lua 5.3

Lua is optional in the sense that if it is not found in the system, a bundled
version will be built and linked statically with WirePlumber. This is controlled
by the **system-lua** meson option.

For building gobject-introspection data, you will also need
`doxygen <https://www.doxygen.nl/>`_ and **g-ir-scanner**.
The latter is usually shipped together with the gobject-introspection
development files.

For building documentation, you will also need
`Sphinx <https://pypi.org/project/Sphinx/>`_,
`sphinx-rtd-theme <https://github.com/readthedocs/sphinx_rtd_theme>`_ and
`breathe <https://pypi.org/project/breathe/>`_.
It is recommended to install those using python's **pip** package manager.

Compilation
-----------

WirePlumber uses the `meson build system <https://mesonbuild.com/>`_

To configure the project, you need to first run `meson`.
The basic syntax is shown below:

.. code:: bash

   meson [--prefix=/path] [...options...] [build directory] [source directory]

Assuming you want to build in a directory called 'build' inside the source
tree, you can run:

.. code:: bash

   $ meson setup --prefix=/usr build
   $ meson compile -C build

Additional options
------------------

.. option:: -Dintrospection=[enabled|disabled|auto]

  Force enable or force disable building gobject-introspection data.

  The default value is **auto**, which means that g-i will be built
  if **doxygen** and **g-ir-scanner** are found and skipped otherwise.

.. option:: -Ddocs=[enabled|disabled|auto]

  Force enable or force disable building documentation.

  The default value is **auto**, which means that documentation will be built
  if **doxygen**, **sphinx** and **breathe** are found and skipped otherwise.

.. option:: -Dsystem-lua=[enabled|disabled|auto]

   Force using lua from the system instead of the bundled one.

   The default value is **auto**, which means that system lua will be used
   if it is found, otherwise the bundled static version will be built silently.

   Use **disabled** to force using the bundled lua.

.. option:: -Dsystemd=[enabled|disabled|auto]

   Enables installing systemd units. The default is **auto**

   **enabled** and **auto** currently mean the same thing.

.. option:: -Dsystemd-system-service=[true|false]

   Enables installing systemd system service file. The default is **false**

.. option:: -Dsystemd-user-service=[true|false]

   Enables installing systemd user service file. The default is **true**

.. option:: -Dsystemd-system-unit-dir=[path]

   Directory for system systemd units.

.. option:: -Dsystemd-user-unit-dir=[path]

   Directory for user systemd units.

.. option:: -Dwpipc=[enabled|disabled|auto]

   Build the wpipc library and module-ipc. The default is **disabled**

   **enabled** and **auto** currently mean the same thing.

   wpipc is small library to send commands directly to WirePlumber; it is
   only useful in specific embedded systems and not recommended for generic use
   (use the PipeWire protocol instead)

Installation
------------

To install, simply run the **install** target with ninja:

.. code:: bash

   $ ninja -C build install

To revert the installation, there is also an **uninstall** target:

.. code:: bash

   $ ninja -C build uninstall
