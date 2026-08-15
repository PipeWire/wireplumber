.. _lua_misc_api:

Miscellaneous
=============

This page collects the smaller globals that the script sandbox exports.

ProcUtils
---------

``ProcUtils`` binds the :ref:`process utilities <proc_utils_api>` C API. It is
used by the access control scripts to find out what a client process actually
is, since a PipeWire client's own properties cannot be trusted for security
decisions.

.. function:: ProcUtils.get_proc_info(pid)

   Binds :c:func:`wp_proc_utils_get_proc_info`

   :param integer pid: the process id to look up
   :returns: information about the process, or nil if it could not be read
   :rtype: ProcInfo

.. function:: ProcInfo.get_pid(self)

   :returns: the process id
   :rtype: integer

.. function:: ProcInfo.get_parent_pid(self)

   :returns: the process id of the parent process
   :rtype: integer

.. function:: ProcInfo.get_cgroup(self)

   :returns: the cgroup of the process, which is how sandboxed applications are
      identified
   :rtype: string

.. function:: ProcInfo.get_n_args(self)

   :returns: the number of command line arguments of the process
   :rtype: integer

.. function:: ProcInfo.get_arg(self, index)

   :param integer index: the index of the argument to return
   :returns: the command line argument at *index*
   :rtype: string

GLib
----

A handful of GLib functions that scripts occasionally need.

.. function:: GLib.get_monotonic_time()

   :returns: the monotonic time in microseconds
   :rtype: integer

.. function:: GLib.get_real_time()

   :returns: the wall clock time in microseconds since the Unix epoch
   :rtype: integer

.. function:: GLib.access(filename, mode)

   Checks whether the given file is accessible in the given mode.

   :param string filename: the path to check
   :param string mode: a combination of the letters ``r``, ``w``, ``x`` and
      ``f``, as in the *access*\ (2) system call
   :returns: true if the file is accessible in that mode
   :rtype: boolean

I18n
----

Translation functions, for scripts that produce user-visible strings.

.. function:: I18n.gettext(msgid)

   :param string msgid: the string to translate
   :returns: the translated string
   :rtype: string

.. function:: I18n.ngettext(msgid, msgid_plural, n)

   Translates a string, choosing the singular or plural form according to *n*.

   :param string msgid: the singular form
   :param string msgid_plural: the plural form
   :param integer n: the number that decides which form is used
   :returns: the translated string
   :rtype: string

Plugin
------

.. function:: Plugin.find(name)

   Binds :c:func:`wp_plugin_find`

   Looks up a loaded plugin by name. This is how scripts reach the API plugins,
   such as *standard-event-source*, *mixer-api* or *default-nodes-api*, whose
   functionality is exposed through GObject action signals; see
   :func:`GObject.call`.

   .. code-block:: lua

      source = Plugin.find ("standard-event-source")

   :param string name: the name of the plugin, without any ``libwireplumber-module-`` prefix
   :returns: the plugin, or nil if it is not loaded
   :rtype: Plugin

Constants
---------

.. describe:: Id

   ``Id.INVALID`` and ``Id.ANY`` both hold the value ``0xffffffff``, used where
   an object id is expected but none is meant.

.. describe:: Perm

   PipeWire client permission flags; see :ref:`lua_permissions_api`.

.. describe:: Feature, Features

   Object feature constants; see :ref:`lua_object_api`.
