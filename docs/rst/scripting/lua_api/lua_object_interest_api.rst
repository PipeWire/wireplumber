 .. _lua_object_interest_api:

Object Interest
===============

The Interest object allows you to declare interest in a specific object, or
a set of objects, and filter them. This is used in the
:ref:`ObjectManager <lua_object_manager_api>` but also in other places where
methods allow you to iterate over a set of objects or lookup a specific object.

*Interest* is a binding of :c:struct:`WpObjectInterest`, but it uses a
Lua-idiomatic way of construction instead of mapping directly the C functions,
for convenience.

Construction
~~~~~~~~~~~~

The most common use for *Interest* is in the
:ref:`ObjectManager <lua_object_manager_api>`, where you have to use the
following construction method to create it.

.. function:: Interest(decl)

   :param table decl: an interest declaration
   :returns: the interest
   :rtype: Interest (:c:struct:`WpObjectInterest`)

The interest consists of a GType and an array of constraints. It is constructed
with a table that contains all the constraints, plus the GType in the ``type``
field.

.. code-block:: lua

   local om = ObjectManager {
     Interest {
       type = "node",
       Constraint { "node.name", "matches", "alsa*", type = "pw-global" },
       Constraint { "media.class", "equals", "Audio/Sink", type = "pw-global" },
     }
   }

In the above example, the interest will match all objects of type
:c:struct:`WpNode` that contain the following 2 global properties:

  - "node.name", with a value that begins with the string "alsa"
  - "media.class", with a value that equals exactly the string "Audio/Sink"

When an object method requires an *Interest* as an argument, you may as well
directly pass the declaration table as argument instead of using the
:func:`Interest` constructor function. For instance,

.. code-block:: lua

   local fl_port = node:lookup_port {
     Constraint { "port.name", "equals", "playback_FL" }
   }

In the above example, we lookup a port in a node. The :func:`Node.lookup_port`
method takes an *Interest* as an argument, but we can pass the interest
declaration table directly. Note also that such a method does not require
declaring the ``type`` of the interest, as it has :c:struct:`WpPort` as a
hardcoded default.

The type
........

The type of an interest must be a valid GType, written as a string without the
"Wp" prefix and with the first letter optionally being lowercase. The type may
match any wireplumber object or interface.

Examples:

============= ============
 type string   parsed as
============= ============
node          WpNode
Node          WpNode
device        WpDevice
plugin        WpPlugin
siLink        WpSiLink
SiLink        WpSiLink
properties    WpProperties
============= ============

The Constraint
..............

The *Constraint* is constructed also with a table, like *Interest* itself. This
table must have the following items in this very strict order:

  - a subject string: a string that specifies the name of a property to match
  - a verb string: a string that specifies the match operation
  - an object, if the verb requires it

The verb may be any verb listed in :c:enum:`WpConstraintVerb`, using either
the nickname of the enum or the character value of it.

Allowed verbs:

============ ========= =======================================
nickname     character value
============ ========= =======================================
"equals"     "="       :c:enum:`WP_CONSTRAINT_VERB_EQUALS`
"not-equals" "!"       :c:enum:`WP_CONSTRAINT_VERB_NOT_EQUALS`
"in-list"    "c"       :c:enum:`WP_CONSTRAINT_VERB_IN_LIST`
"in-range"   "~"       :c:enum:`WP_CONSTRAINT_VERB_IN_RANGE`
"matches"    "#"       :c:enum:`WP_CONSTRAINT_VERB_MATCHES`
"is-present" "+"       :c:enum:`WP_CONSTRAINT_VERB_IS_PRESENT`
"is-absent"  "-"       :c:enum:`WP_CONSTRAINT_VERB_IS_ABSENT`
============ ========= =======================================

The values that are expected for each verb are primarily documented in
:c:func:`wp_object_interest_add_constraint`. In Lua, though, native types are
expected instead of GVariant and then they are converted according to the rules
documented in the :ref:`GObject Integration <lua_gobject>` page.

Examples of constraints:

.. code-block:: lua

   Constraint { "node.id", "equals", "42" }
   Constraint { "node.id", "equals", 42 }
   Constraint { "port.physical", "=", true }

   Constraint { "audio.channel", "not-equals", "FL" }

   Constraint { "node.name", "matches", "v4l2_input*" }
   Constraint { "format.dsp", "#", "*mono audio*" }

   -- matches either "default" or "settings" as a possible value for "metadata.name"
   Constraint { "metadata.name", "in-list", "default", "settings" }

   -- matches any priority.session between 0 and 1000, inclusive
   Constraint { "priority.session", "in-range", 0, 1000 }

   -- matches when the object has a "media.role" property
   Constraint { "media.role", "is-present" }

   -- matches when "media.role" is not present in the properties list
   Constraint { "media.role", "is-absent" }

Constraint types
................

PipeWire objects have multiple properties lists, therefore constraints also need
to have a way to specify on which property list they apply. The constraint type
may be any of the types listed in :c:enum:`WpConstraintType` and can be
specified with an additional field in the *Constraint* construction table,
called ``type``.

For instance:

.. code-block:: lua

   Constraint { "node.id", "equals", "42", type = "pw-global" }
   Constraint { "api.alsa.card.name", "matches", "*Intel*", type = "pw" }
   Constraint { "bound-id", "=", 42, type = "gobject" }

Valid types are:

========= ===============================================
type      value
========= ===============================================
pw-global :c:enum:`WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY`
pw        :c:enum:`WP_CONSTRAINT_TYPE_PW_PROPERTY`
gobject   :c:enum:`WP_CONSTRAINT_TYPE_G_PROPERTY`
========= ===============================================

Methods
~~~~~~~

.. function:: Interest.matches(self, obj)

   Binds :c:func:`wp_object_interest_matches`

   Checks if a specific object matches the interest. The object may be a GObject
   or a table convertible to :c:struct:`WpProperties`, if the interest is for
   properties.

   This is rarely useful to use directly on objects, but it may be useful to
   check if a specific table contains key-value pairs that match specific
   constraints, using "properties" as the interest type and passing a table as
   the object.

   :param self: the interest
   :param obj: an object to check for a match
   :type obj: GObject or table
   :returns: whether the object matches the interest
   :rtype: boolean
