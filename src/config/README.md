WirePlumber Configuration
===

This directory contains WirePlumber's configuration files.
There are 4 kinds of files:

* `wireplumber.conf`

  This file lists the modules that are being loaded by WirePlumber.

* `*.endpoint` files

  These files contain rules to create endpoints when WirePlumber detects
  new nodes in the pipewire graph. They are TOML v0.5 files.

  The `00-stream-*.endpoint` files contain rules to create endpoints for the
  nodes of the client applications that connect to pipewire. You should not
  change or remove those unless you know what you are doing.

  The rest of the `.endpoint` files contain rules to create endpoints for
  ALSA device nodes.

  TODO: more info

* `*.endpoint-link` files

  These files contain rules to link endpoints with each other. They are part
  of the policy module.

* `*.streams` files

  These files contain a list of streams and their priorities.

  The names of the streams are used to create streams on new endpoints.
  In order to use a specific list of streams for a specific endpoint,
  the relevant `.endpoint` file that contains the creation rule for that
  endpoint must reference the `.streams` file.

  The stream priorities are being interpreted by the policy module to apply
  restrictions on which app can use the device at a given time.

  The `media.role` of the application's stream is matched against the names
  of the streams when applying policy.
