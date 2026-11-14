/* WirePlumber
 *
 * Copyright © 2021 Asymptotic
 *    @author Arun Raghavan <arun@asymptotic.io>
 *
 * SPDX-License-Identifier: MIT
 */

#include "module.h"
#include "log.h"
#include "error.h"
#include "private/context.h"

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
 *
 * The module is loaded when the WP_IMPL_MODULE_FEATURE_LOADED feature is
 * activated and unloaded when that feature is deactivated, or when the object
 * is destroyed.
 *
 * \remarks When the "client-context" component is loaded, which is the case in
 * the default daemon configuration, the module is loaded in a secondary
 * `pw_context` that runs on its own thread, so that it is not affected by
 * anything that blocks WirePlumber's main loop. The WpImplModule itself stays
 * on the thread that created it and its API is unchanged; the module's own
 * callbacks, though, run on that other thread. Loading the module there does
 * not block the main loop, but unloading it waits until it is done, so that
 * it is gone when deactivation returns. If the component is not loaded
 * at the time the module is loaded, the module is loaded in the core's
 * `pw_context` instead.
 */

typedef struct _WpModuleLoadData WpModuleLoadData;

struct _WpModuleLoadData
{
  WpImplModule *self;
  WpTransition *transition;
  struct pw_context *context;
  struct pw_properties *props;
  gboolean loaded;
  int errsv;
};

struct _WpImplModule
{
  WpObject parent;

  /* Props */
  gchar *name;
  gchar *args;
  WpProperties *props;

  /* Set when the module lives in the client context, on another thread */
  WpClientContext *client_ctx;

  /* Load info */
  struct pw_impl_module *pw_impl_module;
  struct spa_hook impl_module_listener;
  WpModuleLoadData *pending_load;
};

G_DEFINE_TYPE (WpImplModule, wp_impl_module, WP_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_NAME,
  PROP_ARGS,
  PROP_PROPERTIES,
  N_PROPS,
};

static GParamSpec *properties[N_PROPS] = { NULL, };

enum {
  STEP_LOAD = WP_TRANSITION_STEP_CUSTOM_START,
};

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
do_load (gpointer data)
{
  WpModuleLoadData *d = data;
  WpImplModule *self = d->self;

  /* pw_context_load_module() takes ownership of the properties */
  self->pw_impl_module = pw_context_load_module (d->context, self->name,
      self->args, g_steal_pointer (&d->props));

  /* errno is per-thread, so it must be read on the thread that loaded */
  if (self->pw_impl_module) {
    pw_impl_module_add_listener (self->pw_impl_module,
        &self->impl_module_listener, &impl_module_events, self);
    d->loaded = TRUE;
  } else {
    d->errsv = errno;
  }
}

static void
do_unload (gpointer data)
{
  WpImplModule *self = data;

  /* pw_impl_module_destroy() triggers the module's "free" event, which sets
     self->pw_impl_module to NULL */
  if (self->pw_impl_module)
    pw_impl_module_destroy (self->pw_impl_module);
}

static void
wp_impl_module_init (WpImplModule * self)
{
}

static void
wp_impl_module_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  WpImplModule *self = WP_IMPL_MODULE (object);

  switch (prop_id) {
    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;

    case PROP_ARGS:
      g_value_set_string (value, self->args);
      break;

    case PROP_PROPERTIES:
      g_value_set_boxed (value, self->props);
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

  switch (prop_id) {
    case PROP_NAME:
      g_free (self->name);
      self->name = g_value_dup_string (value);
      break;

    case PROP_ARGS:
      g_free (self->args);
      self->args = g_value_dup_string (value);
      break;

    case PROP_PROPERTIES:
      g_clear_pointer (&self->props, wp_properties_unref);
      self->props = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static WpObjectFeatures
wp_impl_module_get_supported_features (WpObject * object)
{
  return WP_IMPL_MODULE_FEATURE_LOADED;
}

static guint
wp_impl_module_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_IMPL_MODULE_FEATURE_LOADED,
      WP_TRANSITION_STEP_ERROR);

  return STEP_LOAD;
}

static void
unload_module (WpImplModule * self)
{
  if (self->client_ctx)
    wp_client_context_run_in_thread_sync (self->client_ctx, do_unload, self);
  else
    do_unload (self);
  g_clear_object (&self->client_ctx);
}

static void
load_data_free (WpModuleLoadData * d)
{
  g_clear_pointer (&d->props, pw_properties_free);
  g_clear_object (&d->transition);
  g_clear_object (&d->self);
  g_free (d);
}

/* completes the load on this thread; takes ownership of @error, which is set
   if the result of the load could not be handed back to this thread */
static void
finish_load (WpModuleLoadData * d, GError * error)
{
  WpImplModule *self = d->self;

  self->pending_load = NULL;

  /* the load failed, or the activation was aborted in the meantime */
  if (error || !d->loaded || wp_transition_get_completed (d->transition)) {
    if (d->loaded)
      unload_module (self);
    else
      g_clear_object (&self->client_ctx);

    if (!wp_transition_get_completed (d->transition)) {
      if (error)
        g_prefix_error (&error, "failed to load module '%s': ", self->name);
      else
        error = g_error_new (WP_DOMAIN_LIBRARY,
            WP_LIBRARY_ERROR_OPERATION_FAILED, "failed to load module '%s': %s",
            self->name, g_strerror (d->errsv));
      wp_transition_return_error (d->transition, g_steal_pointer (&error));
    }

    g_clear_error (&error);
    load_data_free (d);
    return;
  }

  /* the caller loses the isolation the client context provides if it is not
     loaded yet, so make it visible which context ended up hosting this */
  wp_debug_object (self, "loaded '%s' in the %s context", self->name,
      self->client_ctx ? "client" : "main");

  wp_object_update_features (WP_OBJECT (self),
      WP_IMPL_MODULE_FEATURE_LOADED, 0);
  load_data_free (d);
}

static void
on_load_done (GObject * source, GAsyncResult * res, gpointer data)
{
  GError *error = NULL;

  wp_client_context_run_in_thread_finish (WP_CLIENT_CONTEXT (source), res,
      &error);
  finish_load (data, error);
}

static void
impl_module_load (WpImplModule * self, WpTransition * transition)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpContext) ctx = NULL;
  struct pw_context *context;
  WpModuleLoadData *d;

  /* if a previous activation was aborted while the module was being loaded,
     take over that load instead of starting a second one */
  if (self->pending_load) {
    g_object_unref (self->pending_load->transition);
    self->pending_load->transition = g_object_ref (transition);
    return;
  }

  if (!self->name) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVALID_ARGUMENT, "the module has no name"));
    return;
  }

  ctx = WP_CONTEXT (wp_client_context_find (core));
  if (!ctx)
    ctx = WP_CONTEXT (g_object_ref (wp_core_get_main_context (core)));

  context = wp_context_get_pw_context (ctx);
  if (!context) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to load module '%s': no pw_context", self->name));
    return;
  }

  if (WP_IS_CLIENT_CONTEXT (ctx))
    self->client_ctx = WP_CLIENT_CONTEXT (g_object_ref (ctx));

  d = g_new0 (WpModuleLoadData, 1);
  d->self = g_object_ref (self);
  d->transition = g_object_ref (transition);
  d->context = context;
  if (self->props)
    d->props = wp_properties_to_pw_properties (self->props);

  self->pending_load = d;

  if (self->client_ctx) {
    wp_client_context_run_in_thread (self->client_ctx, do_load, d,
        on_load_done, d);
  } else {
    do_load (d);
    finish_load (d, NULL);
  }
}

static void
wp_impl_module_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpImplModule *self = WP_IMPL_MODULE (object);

  switch (step) {
  case STEP_LOAD:
    impl_module_load (self, WP_TRANSITION (transition));
    break;
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_impl_module_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpImplModule *self = WP_IMPL_MODULE (object);

  if (!(features & WP_IMPL_MODULE_FEATURE_LOADED))
    return;

  unload_module (self);

  wp_object_update_features (object, 0, WP_IMPL_MODULE_FEATURE_LOADED);
}

static void
wp_impl_module_finalize (GObject * object)
{
  WpImplModule *self = WP_IMPL_MODULE (object);

  /* Props */
  g_free (self->name);
  g_free (self->args);
  g_clear_pointer (&self->props, wp_properties_unref);

  G_OBJECT_CLASS (wp_impl_module_parent_class)->finalize (object);
}

static void
wp_impl_module_class_init (WpImplModuleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  gobject_class->finalize = wp_impl_module_finalize;
  gobject_class->get_property = wp_impl_module_get_property;
  gobject_class->set_property = wp_impl_module_set_property;

  wpobject_class->get_supported_features =
      wp_impl_module_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_impl_module_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_impl_module_activate_execute_step;
  wpobject_class->deactivate = wp_impl_module_deactivate;

  properties[PROP_NAME] = g_param_spec_string ("name", "Name",
      "The name of the PipeWire module", NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_ARGS] = g_param_spec_string ("args", "Args",
      "The arguments to provide to the module while loading", NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PROPERTIES] = g_param_spec_boxed ("properties", "Properties",
      "Additional properties to provide to the module while loading",
      WP_TYPE_PROPERTIES,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPS, properties);
}

/*!
 * \brief Creates a new PipeWire module object, without loading it
 *
 * The module is loaded when the WP_IMPL_MODULE_FEATURE_LOADED feature is
 * activated, and unloaded when that feature is deactivated.
 *
 * \ingroup wpimplmodule
 * \since 0.6.0
 * \param core (transfer none): The WirePlumber core
 * \param name (transfer none): the name of the module to load
 * \param args (nullable) (transfer none): arguments to be passed to the module
 * \param properties (nullable) (transfer none): additional properties to be
 *    provided to the module
 * \returns (transfer full): the new WpImplModule
 */
WpImplModule *
wp_impl_module_new (WpCore * core, const gchar * name,
    const gchar * args, WpProperties * properties)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  g_return_val_if_fail (name, NULL);

  return g_object_new (WP_TYPE_IMPL_MODULE,
      "core", core,
      "name", name,
      "args", args,
      "properties", properties,
      NULL);
}

/*!
 * \brief Gets the name of the PipeWire module
 *
 * \ingroup wpimplmodule
 * \since 0.6.0
 * \param self the module
 * \returns the name of the module
 */
const gchar *
wp_impl_module_get_name (WpImplModule * self)
{
  g_return_val_if_fail (WP_IS_IMPL_MODULE (self), NULL);
  return self->name;
}

/*!
 * \brief Gets the arguments that are passed to the module when it is loaded
 *
 * \ingroup wpimplmodule
 * \since 0.6.0
 * \param self the module
 * \returns (nullable): the arguments of the module
 */
const gchar *
wp_impl_module_get_args (WpImplModule * self)
{
  g_return_val_if_fail (WP_IS_IMPL_MODULE (self), NULL);
  return self->args;
}

/*!
 * \brief Gets the additional properties that are provided to the module when
 *   it is loaded
 *
 * \ingroup wpimplmodule
 * \since 0.6.0
 * \param self the module
 * \returns (transfer full) (nullable): the properties of the module
 */
WpProperties *
wp_impl_module_get_properties (WpImplModule * self)
{
  g_return_val_if_fail (WP_IS_IMPL_MODULE (self), NULL);
  return self->props ? wp_properties_ref (self->props) : NULL;
}
