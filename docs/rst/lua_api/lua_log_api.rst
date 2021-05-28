.. _lua_log_api:

Debug Logging
=============

.. function:: Log.warning(object, message)

   Logs a warning message, like :c:macro:`wp_warning_object`

   :param GObject object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :param string message: the warning message to log

.. function:: Log.message(object, message)

   Logs a normal message, like :c:macro:`wp_message_object`

   :param GObject object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :param string message: the normal message to log

.. function:: Log.info(object, message)

   Logs a info message, like :c:macro:`wp_info_object`

   :param GObject object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :param string message: the info message to log

.. function:: Log.debug(object, message)

   Logs a debug message, like :c:macro:`wp_debug_object`

   :param GObject object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :param string message: the debug message to log

.. function:: Log.trace(object, message)

   Logs a trace message, like :c:macro:`wp_trace_object`

   :param GObject object: optional object to associate the message with; you
      may skip this and just start with the *message* as the first parameter
   :param string message: the trace message to log

.. function:: Debug.dump_table(t)

   Prints a table with all its contents, recursively, to stdout
   for debugging purposes

   :param table t: any table
