/* WirePlumber
 *
 * Copyright © 2019-2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "graph-loader.h"
#include "anatole-config-parser.h"
#include <pipewire/keys.h>
#include <anatole.h>

#define CONFIG_EXTENSION "graph_loader.d/lua"

G_DEFINE_QUARK (wp-m-lua-graph-loader-id, id);
G_DEFINE_QUARK (wp-m-lua-graph-loader-children, children);
G_DEFINE_QUARK (wp-m-lua-graph-loader-callbacks, callbacks);

typedef enum {
  OBJECT_TYPE_MONITOR,
  OBJECT_TYPE_DEVICE,
  OBJECT_TYPE_EXPORTED_DEVICE,
  OBJECT_TYPE_NODE,
  OBJECT_TYPE_EXPORTED_NODE,
} ObjectType;

struct _WpLuaGraphLoader
{
  WpPlugin parent;

  WpCore *local_core;
  WpAnatoleConfigParser *parser;
  GPtrArray *objects;

  /* scope for lua callbacks */
  gboolean creating_child;
  GList *children_in_scope;
};

G_DEFINE_TYPE (WpLuaGraphLoader, wp_lua_graph_loader, WP_TYPE_PLUGIN)

static void
wp_lua_graph_loader_init (WpLuaGraphLoader * self)
{
}

static void
free_children (GList * children)
{
  g_list_free_full (children, g_object_unref);
}

static void
find_child (GObject * parent, guint32 id, GList ** children, GList ** link,
    GObject ** child)
{
  *children = g_object_steal_qdata (parent, children_quark ());

  /* Find the child */
  for (*link = *children; *link != NULL; *link = g_list_next (*link)) {
    *child = G_OBJECT ((*link)->data);
    guint32 child_id = GPOINTER_TO_UINT (g_object_get_qdata (*child, id_quark ()));
    if (id == child_id)
      break;
  }
}

static void
on_object_info (WpSpaDevice * device,
    guint id, GType type, const gchar * spa_factory,
    WpProperties * props, WpProperties * parent_props,
    WpLuaGraphLoader * self)
{
  AnatoleEngine *engine = wp_anatole_config_parser_get_engine (self->parser);
  GList *link = NULL;
  GObject *child = NULL;

  self->creating_child = TRUE;

  /* Find the child */
  find_child (G_OBJECT (device), id, &self->children_in_scope, &link, &child);

  /* new object, construct... */
  if (type != G_TYPE_NONE && !link) {
    if (type == WP_TYPE_DEVICE || type == WP_TYPE_NODE) {
      /* create through a lua callback, if specified */
      const gchar *create_child_cb = NULL;
      GVariant *callbacks =
          g_object_get_qdata (G_OBJECT (device), callbacks_quark ());

      if (callbacks)
        g_variant_lookup (callbacks, "create-child", "&s", &create_child_cb);

      if (create_child_cb) {
        GVariantBuilder b;
        g_autoptr (GError) error = NULL;
        g_autoptr (WpProperties) properties = wp_properties_ref (props);
        const gchar *type_str = NULL;

        if (type == WP_TYPE_DEVICE) {
          type_str = "device";
        } else if (type == WP_TYPE_NODE) {
          type_str = "node";

          /* add device id property */
          properties = wp_properties_ensure_unique_owner (properties);
          wp_properties_setf (properties, PW_KEY_DEVICE_ID, "%u",
              wp_spa_device_get_bound_id (device));
        }

        /* create the call arguments */
        g_variant_builder_init (&b, G_VARIANT_TYPE ("(ussa{sv}a{sv})"));
        g_variant_builder_add (&b, "u", id);
        g_variant_builder_add (&b, "s", type_str);
        g_variant_builder_add (&b, "s", spa_factory);
        g_variant_builder_add (&b, "@a{sv}",
            wp_properties_to_gvariant (properties));
        g_variant_builder_add (&b, "@a{sv}",
            wp_properties_to_gvariant (parent_props));

        /* fire the callback */
        anatole_engine_call_function (engine, create_child_cb,
            g_variant_builder_end (&b), &error);
        if (error) {
          wp_message_object (self, "call to '%s' failed: %s", create_child_cb,
              error->message);
        }
      }
      else {
        wp_message_object (self, "not creating child; no callback specified");
      }
    } else {
      wp_warning_object (self, "got device object-info for unknown object "
          "type %s", g_type_name (type));
    }
  }
  /* object removed, delete... */
  else if (type == G_TYPE_NONE && link) {
    g_object_unref (child);
    self->children_in_scope = g_list_delete_link (self->children_in_scope, link);
  }

  /* put back the children */
  g_object_set_qdata_full (G_OBJECT (device), children_quark (),
      self->children_in_scope, (GDestroyNotify) free_children);

  self->children_in_scope = NULL;
  self->creating_child = FALSE;
}

static void
device_created (GObject * device, GAsyncResult * res, gpointer user_data)
{
  WpLuaGraphLoader * self = user_data;
  g_autoptr (GError) error = NULL;

  if (!wp_spa_device_export_finish (WP_SPA_DEVICE (device), res, &error)) {
    wp_warning_object (self, "%s", error->message);
    return;
  }

  wp_spa_device_activate (WP_SPA_DEVICE (device));
}

static void
augment_done (GObject * proxy, GAsyncResult * res, gpointer user_data)
{
  WpLuaGraphLoader * self = user_data;
  g_autoptr (GError) error = NULL;

  if (!wp_proxy_augment_finish (WP_PROXY (proxy), res, &error))
    wp_warning_object (self, "%s", error->message);
}

static void
create_object (WpLuaGraphLoader * self, GVariant * object_desc)
{
  const gchar *type_str = NULL;
  const gchar *factory = NULL;
  g_autoptr (GVariant) vproperties = NULL;
  g_autoptr (WpProperties) properties = NULL;
  g_autoptr (GObject) object = NULL;
  ObjectType type;

  g_return_if_fail (g_variant_is_of_type (object_desc,
          G_VARIANT_TYPE_DICTIONARY));

  if (wp_log_level_is_enabled (G_LOG_LEVEL_DEBUG)) {
    g_autofree gchar *tmp = g_variant_print (object_desc, FALSE);
    wp_debug_object (self, "creating: %s", tmp);
  }

  /* determine the type */
  if (!g_variant_lookup (object_desc, "type", "&s", &type_str)) {
    wp_message_object (self, "object 'type' was not specified");
    goto error;
  }
  else if (!g_strcmp0 (type_str, "monitor"))
    type = OBJECT_TYPE_MONITOR;
  else if (!g_strcmp0 (type_str, "device"))
    type = OBJECT_TYPE_DEVICE;
  else if (!g_strcmp0 (type_str, "exported-device"))
    type = OBJECT_TYPE_EXPORTED_DEVICE;
  else if (!g_strcmp0 (type_str, "node"))
    type = OBJECT_TYPE_NODE;
  else if (!g_strcmp0 (type_str, "exported-node"))
    type = OBJECT_TYPE_EXPORTED_NODE;
  else {
    wp_message_object (self, "Invalid object type: %s", type_str);
    goto error;
  }

  /* retrieve common fields */
  g_variant_lookup (object_desc, "factory", "&s", &factory);
  g_variant_lookup (object_desc, "properties", "@a{sv}", &vproperties);
  if (vproperties)
    properties = wp_properties_new_from_gvariant (vproperties);

  /* create the object */
  switch (type) {
    case OBJECT_TYPE_MONITOR:
    case OBJECT_TYPE_EXPORTED_DEVICE:
      object = (GObject *) wp_spa_device_new_from_spa_factory (self->local_core,
          factory, g_steal_pointer (&properties));
      break;
    case OBJECT_TYPE_DEVICE:
      object = (GObject *) wp_device_new_from_factory (self->local_core,
          factory, g_steal_pointer (&properties));
      break;
    case OBJECT_TYPE_NODE:
      object = (GObject *) wp_node_new_from_factory (self->local_core,
          factory, g_steal_pointer (&properties));
      break;
    case OBJECT_TYPE_EXPORTED_NODE:
      object = (GObject *) wp_impl_node_new_from_pw_factory (self->local_core,
          factory, g_steal_pointer (&properties));
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (!object)
    goto error;

  /* activate */
  switch (type) {
    case OBJECT_TYPE_MONITOR:
    case OBJECT_TYPE_EXPORTED_DEVICE: {
      GVariant *callbacks = NULL;

      if (g_variant_lookup (object_desc, "callbacks", "@a{sv}", &callbacks)) {
        g_object_set_qdata_full (object, callbacks_quark (),
            callbacks, (GDestroyNotify) g_variant_unref);
      }
      g_signal_connect (object,
          "object-info", (GCallback) on_object_info, self);

      if (type == OBJECT_TYPE_MONITOR)
        wp_spa_device_activate (WP_SPA_DEVICE (object));
      else
        wp_spa_device_export (WP_SPA_DEVICE (object), NULL, device_created,
            self);
      break;
    }
    case OBJECT_TYPE_DEVICE:
    case OBJECT_TYPE_NODE:
      wp_proxy_augment (WP_PROXY (object), WP_PROXY_FEATURES_STANDARD, NULL,
          augment_done, self);
      break;
    case OBJECT_TYPE_EXPORTED_NODE:
      wp_impl_node_export (WP_IMPL_NODE (object));
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  /* store */
  if (self->creating_child) {
    gint64 child_id;

    if (!g_variant_lookup (object_desc, "child_id", "x", &child_id)) {
      wp_message_object (self, "attempted to create a child object without "
          "a 'child_id'");
      goto error;
    }

    g_object_set_qdata (object, id_quark (), GINT_TO_POINTER (child_id));
    self->children_in_scope = g_list_prepend (self->children_in_scope,
        g_steal_pointer (&object));
  } else {
    g_ptr_array_add (self->objects, g_steal_pointer (&object));
  }

  return;

error:
  {
    g_autofree gchar *tmp = g_variant_print (object_desc, FALSE);
    wp_message_object (self, "failed to create object: %s", tmp);
  }
}

static void
load_objects (WpLuaGraphLoader * self)
{
  AnatoleEngine *engine = wp_anatole_config_parser_get_engine (self->parser);
  g_autoptr (GVariant) objects =
      anatole_engine_get_global_variable (engine, "objects");
  GVariantIter iter;
  GVariant *object_desc;

  if (!objects || !g_variant_is_of_type (objects, G_VARIANT_TYPE_DICTIONARY)) {
    wp_message_object (self, "No 'objects' dictionary was located in the "
        "graph loader script");
    return;
  }

  g_variant_iter_init (&iter, objects);
  while (g_variant_iter_loop (&iter, "{sv}", NULL, &object_desc)) {
    create_object (self, object_desc);
  }
}

static GVariant *
lua_create_object (AnatoleEngine * engine, GVariant * args, gpointer data)
{
  WpLuaGraphLoader * self = WP_LUA_GRAPH_LOADER (data);
  g_autoptr (GVariant) dict = NULL;
  g_variant_get (args, "(@a{sv})", &dict);
  create_object (self, dict);
  return NULL;
}

static GVariant *
lua_debug (AnatoleEngine * engine, GVariant * args, gpointer data)
{
  const gchar * s = NULL;
  g_variant_get (args, "(&s)", &s);
  wp_debug_object (data, "%s", s);
  return NULL;
}

static GVariant *
lua_trace (AnatoleEngine * engine, GVariant * args, gpointer data)
{
  const gchar * s = NULL;
  g_variant_get (args, "(&s)", &s);
  wp_trace_object (data, "%s", s);
  return NULL;
}

static void
load_functions (gpointer p, AnatoleEngine * engine, WpLuaGraphLoader * self)
{
  g_autoptr (GError) error = NULL;

  anatole_engine_add_function (engine,
      "create_object", lua_create_object, "(a{sv})", self, NULL);
  anatole_engine_add_function (engine,
      "debug", lua_debug, "(s)", self, NULL);
  anatole_engine_add_function (engine,
      "trace", lua_trace, "(s)", self, NULL);

  if (!anatole_engine_add_function_finish (engine, &error)) {
    wp_critical_object (self, "failed to load lua functions: %s",
        error->message);
    return;
  }

  if (!anatole_engine_load_script (engine,
          "resource:///org/freedesktop/pipewire/wireplumber/graph-loader-lib.lua",
          &error)) {
    wp_critical_object (self, "%s", error->message);
    return;
  }
}

static void
wp_lua_graph_loader_activate (WpPlugin * plugin)
{
  WpLuaGraphLoader * self = WP_LUA_GRAPH_LOADER (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);
  g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
  g_autoptr (GError) error = NULL;

  /* initialize secondary connection to pipewire */
  self->local_core = wp_core_clone (core);
  wp_core_update_properties (self->local_core, wp_properties_new (
        PW_KEY_APP_NAME, "WirePlumber (graph loader)",
        NULL));
  if (!wp_core_connect (self->local_core)) {
    wp_warning_object (self, "failed to connect graph loader core");
    return;
  }

  /* load the engine */
  wp_configuration_add_extension (config, CONFIG_EXTENSION,
      WP_TYPE_ANATOLE_CONFIG_PARSER);
  self->parser = (WpAnatoleConfigParser *)
      wp_configuration_get_parser (config, CONFIG_EXTENSION);
  g_signal_connect (self->parser, "load-functions",
      G_CALLBACK (load_functions), self);

  /* load the config files */
  wp_configuration_reload (config, CONFIG_EXTENSION);

  /* load the graph */
  self->objects = g_ptr_array_new_with_free_func (g_object_unref);
  load_objects (self);
}

static void
wp_lua_graph_loader_deactivate (WpPlugin * plugin)
{
  WpLuaGraphLoader * self = WP_LUA_GRAPH_LOADER (plugin);
  g_autoptr (WpCore) core = wp_plugin_get_core (plugin);

  if (core) {
    g_autoptr (WpConfiguration) config = wp_configuration_get_instance (core);
    wp_configuration_remove_extension (config, CONFIG_EXTENSION);
  }
  g_clear_object (&self->parser);
  g_clear_pointer (&self->objects, g_ptr_array_unref);
  g_clear_object (&self->local_core);
}

static void
wp_lua_graph_loader_class_init (WpLuaGraphLoaderClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  plugin_class->activate = wp_lua_graph_loader_activate;
  plugin_class->deactivate = wp_lua_graph_loader_deactivate;
}
