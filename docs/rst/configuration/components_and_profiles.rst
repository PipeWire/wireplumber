.. _config_components_and_profiles:

Components & Profiles
=====================

WirePlumber is organized in components and profiles. Components are
functional parts that provide a specific feature, while profiles are
collections of components that are loaded together to offer a certain
overall experience.

Components
----------

Components are functional parts that provide a specific feature. They can be
described by a name, a type, a feature that they provide and a set of
dependencies, required and optional.

In the configuration file, a component is described as a SPA-JSON object,
in the ``wireplumber.components`` array section, like this:

  .. code-block::

     {
        name = <component-name>
        type = <component-type>
        arguments = { <json object> }

        # Feature that this component provides
        provides = <feature>

        # List of features that must be provided before this component is loaded
        requires = [ <features> ]

        # List of features that would offer additional functionality if provided
        # but are not strictly required
        wants = [ <features> ]
     }

Name & arguments
~~~~~~~~~~~~~~~~

The name identifies the resource that this component loads. For example,
it can be a file or a shared library. Depending on the type, the component
may also accept arguments, which are passed on to the resource when it is
loaded.

Types
~~~~~

The main types of components are:

  * **script/lua**

    A Lua script, which usually contains one or more event hooks and/or
    other custom logic. This is the main type of component as WirePlumber's
    business logic is mostly written in Lua.

  * **module**

    A WirePlumber module, which is a shared library that can be loaded
    dynamically. Modules usually provide some bundled logic to be consumed by
    scripts or some integration between WirePlumber and an external service.

  * **virtual**

    Virtual components are just load targets that can be used to pull in
    other components by defining dependencies. They do not provide any
    functionality by themselves. Note that such components do not have a "name".

  * **built-in**

    These components are functional parts that are already built into the
    WirePlumber library. They provide mostly internal support elements and checks.

Features
~~~~~~~~

A "feature" is a name that we can use to refer to what is being provided
by a component. For example, the ``monitors/alsa.lua`` script provides the
``monitor.alsa`` feature. The feature name is used to refer to the component
when defining dependencies between components and also when defining profiles.

When a component loads successfully, its feature is marked as provided,
otherwise it is not. Whether a feature is provided or not can be checked at
runtime in Lua scripts using the :func:`Core.test_feature` function and in C code
using the :c:func:`wp_core_test_feature` function.

For a list of well-known features, see :ref:`config_features`.

Dependencies
~~~~~~~~~~~~

Each component can "provide" a feature. When the component is loaded, the
feature is marked as provided. Other components can either "require"
or "want" a feature.

If a component "requires" a feature, that means that this feature **must** be
provided before this component is loaded and WirePlumber will try to load the
relevant component that provides that feature if it is not already loaded
(i.e. it will pull in the component). If that other component fails to load,
hence the feature is not provided, the component that requires it will fail
to load as well.

If a component "wants" a feature, that means that this feature would be nice
to have, in the sense that it would offer additional functionality if it
was provided, but it's not strictly needed. WirePlumber will also try to load
the relevant component that provides that feature if it is not already loaded,
meaning that it will also pull in the component. However, if that other
component fails to load, the component that wants it will still be loaded
without error.

Profiles
--------

A profile is a collection of components that are loaded together to offer
a certain overall experience.

Profiles are defined in the configuration file as a SPA-JSON object,
in the ``wireplumber.profiles`` section, like this:

  .. code-block::

     <profile> = {
       <feature name> = [ required | optional | disabled ]
       ...
     }

Each feature can be marked as *required*, *optional* or *disabled*.

  * **required**: Loading this profile will pull in the component that can
    provide this feature in and if it fails to load, the profile will fail to
    load as well.
  * **optional**: Loading this profile does not pull in the component that
    can provide this feature. If any of the required components either
    *requires* or *wants* this feature, then WirePlumber will try to load it.
    If it fails to load, the error condition depends on whether this feature was
    required or wanted by the component that pulled it in.
  * **disabled**: This feature will **not** be loaded, even if it is *wanted*
    by some component. If any required component *requires* this feature, then
    the profile will fail to load.

By default, all the features provided by all the components in the
``wireplumber.components`` section are considered to be *optional*.
That means that no component will be loaded on an empty profile, since optional
components are not pulled in automatically.

If a feature is marked as *required* in a profile, then the component that
provides that feature will be pulled in, together with all its dependencies,
both required and optional.

  .. note::

     In essence, all optional features are opt-in by default. To opt out,
     you need to mark the feature as *disabled*.

Dependency chain example
------------------------

Consider the following configuration file:

  .. code-block::

      wireplumber.components = [
        {
          name = libwireplumber-module-dbus-connection, type = module
          provides = support.dbus
        }
        {
          name = libwireplumber-module-reserve-device, type = module
          provides = support.reserve-device
          requires = [ support.dbus ]
        }
        {
          name = monitors/alsa.lua, type = script/lua
          provides = monitor.alsa
          wants = [ support.reserve-device ]
        }
      ]

      wireplumber.profiles = {
        main = {
          monitor.alsa = required
        }
      }

In this example, the ``main`` profile requires the ``monitor.alsa`` feature.
This will cause the ``monitors/alsa.lua`` script to be loaded. Now, since the
``monitors/alsa.lua`` script *wants* the ``support.reserve-device`` feature,
the ``libwireplumber-module-reserve-device`` module will also be pulled in.
And since that one *requires* the ``support.dbus`` feature, the
``libwireplumber-module-dbus-connection`` module will also be pulled in.

However, on a system without D-Bus, a user may want to opt out of the
``libwireplumber-module-dbus-connection`` module. This can be done by marking
the ``support.dbus`` feature as disabled in the profile:

  .. code-block::

     wireplumber.profiles = {
        main = {
          monitor.alsa = required
          support.dbus = disabled
        }
      }

Upon doing that, the ``libwireplumber-module-dbus-connection`` module will
not be loaded, causing the ``libwireplumber-module-reserve-device`` module
to not be loaded as well, since it requires the ``support.dbus`` feature.
The ``monitors/alsa.lua`` script will still be loaded, since it only *wants*
the ``support.reserve-device`` feature.
