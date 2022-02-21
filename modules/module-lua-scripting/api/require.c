/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <wplua/wplua.h>

struct _WpRequireApiTransition
{
  WpTransition parent;
  GPtrArray *apis;
  guint pending_plugins;
};

enum {
  STEP_LOAD_MODULES = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_ACTIVATE_PLUGINS,
};

G_DECLARE_FINAL_TYPE (WpRequireApiTransition, wp_require_api_transition,
                      WP, REQUIRE_API_TRANSITION, WpTransition)
G_DEFINE_TYPE (WpRequireApiTransition, wp_require_api_transition, WP_TYPE_TRANSITION)

static void
wp_require_api_transition_init (WpRequireApiTransition * self)
{
  self->apis = g_ptr_array_new_with_free_func (g_free);
}

static void
wp_require_api_transition_finalize (GObject * object)
{
  WpRequireApiTransition *self = WP_REQUIRE_API_TRANSITION (object);

  g_clear_pointer (&self->apis, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_require_api_transition_parent_class)->finalize (object);
}

static guint
wp_require_api_transition_get_next_step (WpTransition * transition, guint step)
{
  WpRequireApiTransition *self = WP_REQUIRE_API_TRANSITION (transition);

  switch (step) {
  case WP_TRANSITION_STEP_NONE: return STEP_LOAD_MODULES;
  case STEP_LOAD_MODULES:       return STEP_ACTIVATE_PLUGINS;
  case STEP_ACTIVATE_PLUGINS:
    return (self->pending_plugins > 0) ?
        STEP_ACTIVATE_PLUGINS : WP_TRANSITION_STEP_NONE;
  default:
    g_return_val_if_reached (WP_TRANSITION_STEP_ERROR);
  }
}

static void
on_plugin_activated (WpObject * p, GAsyncResult * res,
    WpRequireApiTransition *self)
{
  GError *error = NULL;

  if (!wp_object_activate_finish (p, res, &error)) {
    wp_transition_return_error (WP_TRANSITION (self), error);
    return;
  }

  --self->pending_plugins;
  wp_transition_advance (WP_TRANSITION (self));
}

static void
wp_require_api_transition_execute_step (WpTransition * transition, guint step)
{
  WpRequireApiTransition *self = WP_REQUIRE_API_TRANSITION (transition);
  WpCore *core = wp_transition_get_source_object (transition);

  switch (step) {
  case STEP_LOAD_MODULES:
  {
    for (guint i = 0; i < self->apis->len; i++) {
      const gchar *api_name = g_ptr_array_index (self->apis, i);
      g_autoptr (WpPlugin) plugin = wp_plugin_find (core, api_name);
      if (!plugin) {
        GError *error = NULL;
        gchar module_name[50];

        g_snprintf (module_name, sizeof (module_name),
            "libwireplumber-module-%s", api_name);

        if (!wp_core_load_component (core, module_name, "module", NULL, &error)) {
          wp_transition_return_error (transition, error);
          return;
        }

        plugin = wp_plugin_find (core, api_name);
        if (!plugin) {
          wp_transition_return_error (transition, g_error_new (
              WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
              "API '%s' was not found in module '%s'", api_name, module_name));
          return;
        }
      }
    }
    wp_transition_advance (transition);
    break;
  }

  case STEP_ACTIVATE_PLUGINS:
    wp_debug_object (self, "Activating plugins...");

    for (guint i = 0; i < self->apis->len; i++) {
      const gchar *api_name = g_ptr_array_index (self->apis, i);
      g_autoptr (WpPlugin) plugin = wp_plugin_find (core, api_name);

      self->pending_plugins++;
      wp_object_activate (WP_OBJECT (plugin), WP_PLUGIN_FEATURE_ENABLED, NULL,
          (GAsyncReadyCallback) on_plugin_activated, self);
    }
    wp_transition_advance (transition);
    break;

  case WP_TRANSITION_STEP_ERROR:
    break;

  default:
    g_assert_not_reached ();
  }
}

static void
wp_require_api_transition_class_init (WpRequireApiTransitionClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  WpTransitionClass * transition_class = (WpTransitionClass *) klass;

  object_class->finalize = wp_require_api_transition_finalize;

  transition_class->get_next_step = wp_require_api_transition_get_next_step;
  transition_class->execute_step = wp_require_api_transition_execute_step;
}

static void
on_require_api_transition_done (WpCore * core, GAsyncResult * res, gpointer data)
{
  g_autoptr (GClosure) closure = data;
  g_autoptr (GError) error = NULL;

  if (!wp_transition_finish (res, &error)) {
    wp_warning ("Core.require_api failed: %s", error->message);
    wp_core_idle_add (core, NULL, G_SOURCE_FUNC (core_disconnect), core, NULL);
    return;
  }

  WpRequireApiTransition *t = WP_REQUIRE_API_TRANSITION (res);
  g_autoptr (GArray) params = g_array_new (FALSE, TRUE, sizeof (GValue));

  g_array_set_clear_func (params, (GDestroyNotify) g_value_unset);
  g_array_set_size (params, t->apis->len);

  for (guint i = 0; i < t->apis->len; i++) {
    const gchar *api_name = g_ptr_array_index (t->apis, i);
    g_autoptr (WpPlugin) plugin = wp_plugin_find (core, api_name);
    g_value_init_from_instance (&g_array_index (params, GValue, i), plugin);
  }

  g_closure_invoke (closure, NULL,
      params->len, (const GValue *) params->data, NULL);
  g_closure_invalidate (closure);
}

static int
wp_require_api_transition_new_from_lua (lua_State *L, WpCore * core)
{
  int n_args = lua_gettop (L);

  wp_info("n_args = %d", n_args);

  for (int i = 1; i < n_args; i++)
    luaL_checktype (L, i, LUA_TSTRING);
  luaL_checktype (L, n_args, LUA_TFUNCTION);

  GClosure *closure = wplua_function_to_closure (L, n_args);
  g_closure_ref (closure);
  g_closure_sink (closure);

  WpRequireApiTransition *t = (WpRequireApiTransition *)
      wp_transition_new (wp_require_api_transition_get_type (), core, NULL,
          (GAsyncReadyCallback) on_require_api_transition_done, closure);

  for (int i = 1; i < n_args; i++) {
    const char * api_name = lua_tostring (L, i);
    g_ptr_array_add (t->apis, g_strdup_printf ("%s-api", api_name));
  }

  wp_transition_advance (WP_TRANSITION (t));
  return 0;
}
