/* WirePlumber
 *
 * Copyright © 2021 Asymptotic
 *    @author Arun Raghavan <arun@asymptotic.io>
 *
 * SPDX-License-Identifier: MIT
 */

#include "module.h"
#include "log.h"
#include "private/export-context.h"

#include <pipewire/impl.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-module")

/*! \defgroup wpimplmodule WpImplModule */
/*!
 * \struct WpImplModule
 * \since 0.4.2
 *
 * Used to load PipeWire modules within the WirePlumber process. This is
 * slightly different from other objects in that the module is not exported to
 * PipeWire, but it may create an export objects itself.
 */

struct _WpImplModule
{
  GObject parent;

  GWeakRef core;
  gchar *name;
  gchar *args;
  WpProperties *props; /* only used during module load */

  /* the context that hosts the module; when this is set, the module lives on
     another thread and every access to pw_impl_module below must be done with
     the export context locked. It is NULL when the export-context component is
     not loaded, in which case the module is hosted by the core's own
     pw_context, on this thread, and no locking is needed */
  WpExportContext *ctx;

  struct pw_impl_module *pw_impl_module;
  struct spa_hook impl_module_listener;
};

static inline void
wp_impl_module_lock (WpImplModule * self)
{
  if (self->ctx)
    wp_export_context_lock (self->ctx);
}

static inline void
wp_impl_module_unlock (WpImplModule * self)
{
  if (self->ctx)
    wp_export_context_unlock (self->ctx);
}

G_DEFINE_TYPE (WpImplModule, wp_impl_module, G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_CORE,
  PROP_NAME,
  PROP_ARGUMENTS,
  PROP_PROPERTIES,
  PROP_PW_IMPL_MODULE,
  N_PROPS,
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static void impl_module_free (void *data)
{
  WpImplModule *self = WP_IMPL_MODULE (data);
  self->pw_impl_module = NULL;
}

static const struct pw_impl_module_events impl_module_events = {
  PW_VERSION_IMPL_MODULE_EVENTS,
  .free = impl_module_free,
};

static void
wp_impl_module_init (WpImplModule * self)
{
  g_weak_ref_init (&self->core, NULL);
  self->name = NULL;
  self->args = NULL;
  self->props = NULL;
  self->pw_impl_module = NULL;
}

static void
wp_impl_module_constructed (GObject * object)
{
  WpImplModule *self = WP_IMPL_MODULE (object);
  g_autoptr (WpCore) core = g_weak_ref_get (&self->core);
  struct pw_context *context = NULL;
  struct pw_properties *props = NULL;

  /* prefer the export context, so that the module runs on its own thread and
     is not delayed by whatever WirePlumber's main loop happens to be doing */
  self->ctx = core ? wp_export_context_find (core) : NULL;
  if (self->ctx)
    context = wp_export_context_get_pw_context (self->ctx);
  else if (core)
    context = wp_core_get_pw_context (core);

  if (!core || !context) {
    g_warning ("Tried to load module on unconnected core");
    return;
  }

  if (!self->name) {
    g_warning ("Invalid name while loading warnings");
    return;
  }

  if (self->props)
    props = wp_properties_to_pw_properties (self->props);

  wp_impl_module_lock (self);
  self->pw_impl_module =
    pw_context_load_module (context, self->name, self->args, props);

  if (self->pw_impl_module) {
    pw_impl_module_add_listener (self->pw_impl_module,
        &self->impl_module_listener, &impl_module_events, self);
  }
  wp_impl_module_unlock (self);

  if (self->pw_impl_module) {
    /* the caller loses the isolation the export context provides if it is not
       loaded yet, so make it visible which context ended up hosting this */
    wp_debug_object (self, "loaded '%s' in the %s context", self->name,
        self->ctx ? "export" : "main");

    if (self->props) {
      /* With the module loaded, properties are just passthrough now */
      wp_properties_unref (self->props);
      self->props = NULL;
    }
  }

  G_OBJECT_CLASS (wp_impl_module_parent_class)->constructed (object);
}

static void
wp_impl_module_finalize (GObject * object)
{
  WpImplModule *self = WP_IMPL_MODULE (object);

  g_weak_ref_clear (&self->core);

  wp_impl_module_lock (self);
  if (self->pw_impl_module)
    pw_impl_module_destroy (self->pw_impl_module);
  wp_impl_module_unlock (self);

  g_clear_object (&self->ctx);

  g_free (self->name);
  g_free (self->args);

  if (self->props)
    wp_properties_unref (self->props);

  G_OBJECT_CLASS (wp_impl_module_parent_class)->finalize (object);
}

static void
wp_impl_module_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  WpImplModule *self = WP_IMPL_MODULE (object);

  switch (prop_id) {
    case PROP_CORE:
      g_value_set_pointer (value, g_weak_ref_get (&self->core));
      break;

    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;

    case PROP_ARGUMENTS:
      g_value_set_string (value, self->args);
      break;

    case PROP_PROPERTIES:
      wp_impl_module_lock (self);
      if (self->pw_impl_module) {
        const struct pw_properties *props =
          pw_impl_module_get_properties (self->pw_impl_module);

        /* Should we just wrap instead of copying? */
        if (props)
          g_value_take_boxed (value, wp_properties_new_copy (props));
        else
          g_value_set_boxed (value, NULL);
        wp_impl_module_unlock (self);
      } else {
        wp_impl_module_unlock (self);
        g_value_set_boxed (value, self->props);
      }
      break;

    case PROP_PW_IMPL_MODULE:
      wp_impl_module_lock (self);
      g_value_set_pointer (value, self->pw_impl_module);
      wp_impl_module_unlock (self);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
wp_impl_module_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplModule *self = WP_IMPL_MODULE (object);
  WpProperties *props;

  switch (prop_id) {
    case PROP_CORE:
      g_weak_ref_set (&self->core, g_value_get_pointer (value));
      break;

    case PROP_NAME:
      g_free (self->name);
      self->name = g_value_dup_string (value);
      break;

    case PROP_ARGUMENTS:
      g_free (self->args);
      self->args = g_value_dup_string (value);
      break;

    case PROP_PROPERTIES:
      props = g_value_get_boxed (value);

      wp_impl_module_lock (self);
      if (props && self->pw_impl_module) {
        pw_impl_module_update_properties (self->pw_impl_module,
            wp_properties_peek_dict (props));
        wp_impl_module_unlock (self);
      } else {
        wp_impl_module_unlock (self);
        if (props)
          self->props = wp_properties_ref (props);
        else
          self->props = NULL;
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
wp_impl_module_class_init (WpImplModuleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->constructed = wp_impl_module_constructed;
  gobject_class->finalize = wp_impl_module_finalize;
  gobject_class->get_property = wp_impl_module_get_property;
  gobject_class->set_property = wp_impl_module_set_property;

  properties[PROP_CORE] = g_param_spec_pointer ("core", "Core", "The WirePlumber core",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_NAME] = g_param_spec_string ("name", "Name", "The name of the PipeWire module",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_ARGUMENTS] = g_param_spec_string ("arguments", "Arguments",
      "The arguments to provide to the module while loading", NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PROPERTIES] = g_param_spec_boxed ("properties", "Properties",
      "Properties of the module", WP_TYPE_PROPERTIES,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_PW_IMPL_MODULE] = g_param_spec_pointer ("pw-impl-module", "Underlying pw_impl_module",
      "Pointer to the underlying pw_impl_module structure for the module",
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPS, properties);
}

/*!
 * \brief Loads a PipeWire module into the WirePlumber process
 *
 * \ingroup wpimplmodule
 * \since 0.4.2
 * \param core (transfer none): The WirePlumber core
 * \param name (transfer none): the name of the module to load
 * \param arguments (nullable) (transfer none): arguments to be passed to the module
 * \param properties (nullable) (transfer none): additional properties to be
 *    provided to the module
 * \returns (nullable) (transfer full): the WpImplModule for the module that
 *    was loaded on success, %NULL on failure.
 */
WpImplModule *
wp_impl_module_load (WpCore * core, const gchar * name,
    const gchar * arguments, WpProperties * properties)
{
  gboolean loaded;

  WpImplModule *module = WP_IMPL_MODULE (
      g_object_new (WP_TYPE_IMPL_MODULE,
        "core", core,
        "name", name,
        "arguments", arguments,
        "properties", properties,
        NULL)
      );

  wp_impl_module_lock (module);
  loaded = (module->pw_impl_module != NULL);
  wp_impl_module_unlock (module);

  if (!loaded) {
    /* Module loading failed, free and return */
    g_object_unref (module);
    return NULL;
  }

  return module;
}
