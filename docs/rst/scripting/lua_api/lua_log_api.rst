.. _lua_log_api:

Debug Logging
=============

Constructors
~~~~~~~~~~~~

.. function:: Log.open_topic(topic)

   Opens a LogTopic with the given topic name. Well known script topics are
   described in :ref:`daemon_logging`, and messages from scripts shall use
   **s-***.

   Example:

   .. code-block:: lua

     local obj
     log = Log.open_topic ("s-linking")
     log:info (obj, "an info message on obj")
     log:debug ("a debug message")

   Above example shows how to output debug logs.

   :param string topic: The log topic to open
   :returns: the log topic object
   :rtype: Log (:c:struct:`WpLogTopic`)

Methods
~~~~~~~

.. function:: Log.warning(object, message)

   Logs a warning message, like :c:macro:`wp_warning_object`

   :param object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :type object: GObject or GBoxed
   :param string message: the warning message to log

.. function:: Log.notice(object, message)

   Logs a notice message, like :c:macro:`wp_notice_object`

   :param object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :type object: GObject or GBoxed
   :param string message: the normal message to log

.. function:: Log.info(object, message)

   Logs a info message, like :c:macro:`wp_info_object`

   :param object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :type object: GObject or GBoxed
   :param string message: the info message to log

.. function:: Log.debug(object, message)

   Logs a debug message, like :c:macro:`wp_debug_object`

   :param object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :type object: GObject or GBoxed
   :param string message: the debug message to log

.. function:: Log.trace(object, message)

   Logs a trace message, like :c:macro:`wp_trace_object`

   :param object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :type object: GObject or GBoxed
   :param string message: the trace message to log

.. function:: Debug.dump_table(t)

   Prints a table with all its contents, recursively, to stdout
   for debugging purposes

   :param table t: any table
