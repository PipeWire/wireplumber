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
 * callbacks, though, run on that other thread. If the component is not loaded
 * at the time the module is loaded, the module is loaded in the core's
 * `pw_context` instead.
 */

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

/* loading and destroying the module must happen on the thread of the context
   that hosts it, because the module's own code may do a blocking invoke on the
   context's loop, which would release the lock and let the loop thread run the
   module's callbacks concurrently */

struct load_data {
  WpImplModule *self;
  struct pw_context *context;
  struct pw_properties *props;
  int errsv;
};

static void
do_load (gpointer data)
{
  struct load_data *d = data;
  WpImplModule *self = d->self;

  self->pw_impl_module =
    pw_context_load_module (d->context, self->name, self->args, d->props);

  /* errno is per-thread, so it must be read on the thread that loaded */
  if (self->pw_impl_module) {
    pw_impl_module_add_listener (self->pw_impl_module,
        &self->impl_module_listener, &impl_module_events, self);
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
impl_module_load (WpImplModule * self, WpTransition * transition)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_autoptr (WpContext) ctx = NULL;
  struct load_data d = { .self = self };

  if (!self->name) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVALID_ARGUMENT, "the module has no name"));
    return;
  }

  ctx = WP_CONTEXT (wp_client_context_find (core));
  if (!ctx)
    ctx = WP_CONTEXT (g_object_ref (wp_core_get_main_context (core)));

  d.context = wp_context_get_pw_context (ctx);
  if (!d.context) {
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to load module '%s': no pw_context", self->name));
    return;
  }

  if (WP_IS_CLIENT_CONTEXT (ctx))
    self->client_ctx = WP_CLIENT_CONTEXT (g_object_ref (ctx));

  if (self->props)
    d.props = wp_properties_to_pw_properties (self->props);

  if (self->client_ctx)
    wp_client_context_run_in_thread_sync (self->client_ctx, do_load, &d);
  else
    do_load (&d);

  if (!self->pw_impl_module) {
    g_clear_object (&self->client_ctx);
    wp_transition_return_error (transition, g_error_new (WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to load module '%s': %s", self->name, g_strerror (d.errsv)));
    return;
  }

  /* the caller loses the isolation the client context provides if it is not
     loaded yet, so make it visible which context ended up hosting this */
  wp_debug_object (self, "loaded '%s' in the %s context", self->name,
      self->client_ctx ? "client" : "main");

  wp_object_update_features (WP_OBJECT (self),
      WP_IMPL_MODULE_FEATURE_LOADED, 0);
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

  if (self->client_ctx)
    wp_client_context_run_in_thread_sync (self->client_ctx, do_unload, self);
  else
    do_unload (self);
  g_clear_object (&self->client_ctx);

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
