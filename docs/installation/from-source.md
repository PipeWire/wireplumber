# Installing WirePlumber

## Dependencies

In order to compile WirePlumber you will need:

* GLib >= 2.58
* PipeWire 0.3 (>= 0.3.5 highly recommended)

For building gobject-introspection data, you will also need `g-ir-scanner`,
which is usually shipped together with the gobject-introspection development
files.

For building documentation, you will also need [hotdoc](https://hotdoc.github.io/).
Most distributions do not ship this, but you can install it easily using python's
`pip` package manager.

## Compilation

WirePlumber uses the [meson build system](https://mesonbuild.com/).

To configure the project, you need to first run `meson`.
The basic syntax is shown below:
```
meson [build directory] [source directory] [--prefix=/path] [...options...]
```

Assuming you want to build in a directory called `build` inside the source
tree, you can run:
```
$ meson build . --prefix=/usr
$ ninja -C build
```

### Additional options

- `-Dintrospection=[enabled|disabled|auto]`: Force enable or force disable
  building gobject-introspection data. The default value is `auto`, which means
  that g-i will be built if `g-ir-scanner` is found and skipped otherwise.
- `-Ddocs=[enabled|disabled|auto]`: Force enable or force disable building
  documentation. The default value is `auto`, which means that documentation
  will be built if `hotdoc` is found and skipped otherwise. Note that building
  the documentation also requires gobject-introspection data to be built.

## Installation

To install, simply run the `install` target with ninja:
```
$ ninja -C build install
```

To revert the installation, there is also an `uninstall` target:
```
$ ninja -C build uninstall
```
