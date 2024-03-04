/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"
#include "wp.h"
#include "private/registry.h"
#include "private/internal-comp-loader.h"

#include <pipewire/pipewire.h>

#include <spa/utils/result.h>
#include <spa/debug/types.h>
#include <spa/support/cpu.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-core")

/*
 * Integration between the PipeWire main loop and GMainLoop
 */

#define WP_LOOP_SOURCE(x) ((WpLoopSource *) x)

typedef struct _WpLoopSource WpLoopSource;
struct _WpLoopSource
{
  GSource parent;
  struct pw_loop *loop;
};

static gboolean
wp_loop_source_dispatch (GSource * s, GSourceFunc callback, gpointer user_data)
{
  int result;

  wp_trace_boxed (G_TYPE_SOURCE, s, "entering pw main loop");

  pw_loop_enter (WP_LOOP_SOURCE(s)->loop);
  result = pw_loop_iterate (WP_LOOP_SOURCE(s)->loop, 0);
  pw_loop_leave (WP_LOOP_SOURCE(s)->loop);

  wp_trace_boxed (G_TYPE_SOURCE, s, "leaving pw main loop");

  if (G_UNLIKELY (result < 0))
    wp_warning_boxed (G_TYPE_SOURCE, s,
        "pw_loop_iterate failed: %s", spa_strerror (result));

  return G_SOURCE_CONTINUE;
}

static void
wp_loop_source_finalize (GSource * s)
{
  pw_loop_destroy (WP_LOOP_SOURCE(s)->loop);
}

static GSourceFuncs source_funcs = {
  NULL,
  NULL,
  wp_loop_source_dispatch,
  wp_loop_source_finalize
};

static GSource *
wp_loop_source_new (void)
{
  GSource *s = g_source_new (&source_funcs, sizeof (WpLoopSource));
  WP_LOOP_SOURCE(s)->loop = pw_loop_new (NULL);

  g_source_add_unix_fd (s,
      pw_loop_get_fd (WP_LOOP_SOURCE(s)->loop),
      G_IO_IN | G_IO_ERR | G_IO_HUP);

  return (GSource *) s;
}

/*! \defgroup wpcore WpCore */
/*!
 * \struct WpCore
 *
 * The core is the central object around which everything operates. It is
 * essential to create a WpCore before using any other WirePlumber API.
 *
 * The core object has the following responsibilities:
 *  * it initializes the PipeWire library
 *  * it creates a `pw_context` and allows connecting to the PipeWire server,
 *    creating a local `pw_core`
 *  * it glues the PipeWire library's event loop system with GMainLoop
 *  * it maintains a list of registered objects, which other classes use
 *    to keep objects loaded permanently into memory
 *  * it watches the PipeWire registry and keeps track of remote and local
 *    objects that appear in the registry, making them accessible through
 *    the WpObjectManager API.
 *
 * The core is also responsible for loading components, which are defined in
 * the main configuration file. Components are loaded when
 * WP_CORE_FEATURE_COMPONENTS is activated.
 *
 * \b Configuration
 *
 * The main configuration file needs to be created and opened before the core
 * is created, using the WpConf API. It is then passed to the core as an
 * argument in the constructor.
 *
 * If a configuration file is not provided, the core will let the underlying
 * `pw_context` load its own configuration, based on the rules that apply to
 * all pipewire clients (e.g. it respects the `PIPEWIRE_CONFIG_NAME` environment
 * variable and loads "client.conf" as a last resort).
 *
 * If a configuration file is provided, the core does not let the underlying
 * `pw_context` load any configuration and instead uses the provided WpConf
 * object.
 *
 * \gproperties
 *
 * \gproperty{g-main-context, GMainContext *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   A GMainContext to attach to}
 *
 * \gproperty{properties, WpProperties *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The pipewire properties of the pw_core}
 *
 * \gproperty{pw-context, gpointer (struct pw_context *), G_PARAM_READABLE,
 *   The pipewire context}
 *
 * \gproperty{pw-core, gpointer (struct pw_core *), G_PARAM_READABLE,
 *   The pipewire core}
 *
 * \gproperty{conf, WpConf *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The main configuration file}
 *
 * \gsignals
 *
 * \par connected
 * \parblock
 * \code
 * void
 * connected_callback (WpCore * self,
 *                     gpointer user_data)
 * \endcode
 * Emitted when the core is successfully connected to the PipeWire server
 * \endparblock
 *
 * \par disconnected
 * \parblock
 * \code
 * void
 * disconnected_callback (WpCore * self,
 *                        gpointer user_data)
 * \endcode
 * Emitted when the core is disconnected from the PipeWire server
 * \endparblock
 */
struct _WpCore
{
  WpObject parent;

  /* main loop integration */
  GMainContext *g_main_context;

  /* extra properties */
  WpProperties *properties;

  /* pipewire main objects */
  struct pw_context *pw_context;
  struct pw_core *pw_core;
  struct pw_core_info *info;

  /* pipewire main listeners */
  struct spa_hook core_listener;
  struct spa_hook proxy_core_listener;

  /* the main configuration file */
  WpConf *conf;

  WpRegistry registry;
  GHashTable *async_tasks; // <int seq, GTask*>
};

enum {
  PROP_0,
  PROP_G_MAIN_CONTEXT,
  PROP_PROPERTIES,
  PROP_PW_CONTEXT,
  PROP_PW_CORE,
  PROP_CONF,
};

enum {
  SIGNAL_CONNECTED,
  SIGNAL_DISCONNECTED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

G_DEFINE_TYPE (WpCore, wp_core, WP_TYPE_OBJECT)

static void
core_info (void *data, const struct pw_core_info * info)
{
  WpCore *self = WP_CORE (data);
  gboolean new_connection = (self->info == NULL);

  self->info = pw_core_info_update (self->info, info);

  wp_info_object (self, "connected to server: %s, cookie: %u",
      self->info->name, self->info->cookie);

  if (new_connection) {
    g_signal_emit (self, signals[SIGNAL_CONNECTED], 0);
    wp_object_update_features (WP_OBJECT (self), WP_CORE_FEATURE_CONNECTED, 0);
  }
}

static void
core_done (void *data, uint32_t id, int seq)
{
  WpCore *self = WP_CORE (data);
  g_autoptr (GTask) task = NULL;

  g_hash_table_steal_extended (self->async_tasks, GINT_TO_POINTER (seq), NULL,
      (gpointer *) &task);
  wp_debug_object (self, "done, seq 0x%x, task " WP_OBJECT_FORMAT,
      seq, WP_OBJECT_ARGS (task));

  if (task)
    g_task_return_boolean (task, TRUE);
}

static gboolean
core_disconnect_async (WpCore * self)
{
  wp_core_disconnect (self);
  return G_SOURCE_REMOVE;
}

static void
core_error (void *data, uint32_t id, int seq, int res, const char *message)
{
  WpCore *self = WP_CORE (data);

  /* protocol socket disconnected; schedule disconnecting our core */
  if (id == 0 && res == -EPIPE) {
    wp_core_idle_add_closure (self, NULL, g_cclosure_new_object (
            G_CALLBACK (core_disconnect_async), G_OBJECT (self)));
  }
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .info = core_info,
  .done = core_done,
  .error = core_error,
};

static gboolean
async_tasks_finish (gpointer key, gpointer value, gpointer user_data)
{
  GTask *task = G_TASK (value);
  g_return_val_if_fail (task, FALSE);

  g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT, "core disconnected");
  return TRUE;
}

static void
proxy_core_destroy (void *data)
{
  WpCore *self = WP_CORE (data);
  g_hash_table_foreach_remove (self->async_tasks, async_tasks_finish, NULL);
  g_clear_pointer (&self->info, pw_core_info_free);
  spa_hook_remove(&self->core_listener);
  spa_hook_remove(&self->proxy_core_listener);
  self->pw_core = NULL;
  wp_debug_object (self, "emit disconnected");
  g_signal_emit (self, signals[SIGNAL_DISCONNECTED], 0);
  wp_object_update_features (WP_OBJECT (self), 0, WP_CORE_FEATURE_CONNECTED);
}

static const struct pw_proxy_events proxy_core_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_core_destroy,
};

static void
wp_core_init (WpCore * self)
{
  wp_registry_init (&self->registry);
  self->async_tasks = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  wp_core_register_object (self,
      g_object_new (WP_TYPE_INTERNAL_COMP_LOADER, NULL));
}

static void
wp_core_constructed (GObject *object)
{
  WpCore *self = WP_CORE (object);
  g_autoptr (GSource) source = NULL;

  /* loop */
  source = wp_loop_source_new ();
  g_source_attach (source, self->g_main_context);

  /* context */
  if (!self->pw_context) {
    struct pw_properties *p = NULL;
    const gchar *str = NULL;

    /* use our own configuration file, if specified */
    if (self->conf) {
      wp_info_object (self, "using configuration file: %s",
          wp_conf_get_name (self->conf));

      /* ensure we have our very own properties set,
         since we are going to modify it */
      self->properties = self->properties ?
          wp_properties_ensure_unique_owner (self->properties) :
          wp_properties_new_empty ();

      /* load context.properties */
      wp_conf_section_update_props (self->conf, "context.properties",
          self->properties);

      /* disable loading of a configuration file in pw_context */
      wp_properties_set (self->properties, PW_KEY_CONFIG_NAME, "null");
    }

    /* properties are fully stored in the pw_context, no need to keep a copy */
    p = self->properties ?
        wp_properties_unref_and_take_pw_properties (self->properties) : NULL;
    self->properties = NULL;

    self->pw_context = pw_context_new (WP_LOOP_SOURCE(source)->loop, p,
        sizeof (grefcount));
    g_return_if_fail (self->pw_context);

    /* use the same config option as pipewire to set the log level */
    p = (struct pw_properties *) pw_context_get_properties (self->pw_context);
    if (!g_getenv("WIREPLUMBER_DEBUG") &&
        (str = pw_properties_get(p, "log.level")) != NULL) {
      if (!wp_log_set_level (str))
        wp_warning ("ignoring invalid log.level in config file: %s", str);
    }

    /* parse pw_context specific configuration sections */
    if (self->conf)
      wp_conf_parse_pw_context_sections (self->conf, self->pw_context);

    /* Init refcount */
    grefcount *rc = pw_context_get_user_data (self->pw_context);
    g_return_if_fail (rc);
    g_ref_count_init (rc);
  } else {
    /* Increase refcount */
    grefcount *rc = pw_context_get_user_data (self->pw_context);
    g_return_if_fail (rc);
    g_ref_count_inc (rc);
  }

  G_OBJECT_CLASS (wp_core_parent_class)->constructed (object);
}

static void
wp_core_dispose (GObject * obj)
{
  WpCore *self = WP_CORE (obj);

  wp_registry_clear (&self->registry);
  wp_object_update_features (WP_OBJECT (self), 0, WP_CORE_FEATURE_COMPONENTS);

  G_OBJECT_CLASS (wp_core_parent_class)->dispose (obj);
}

static void
wp_core_finalize (GObject * obj)
{
  WpCore *self = WP_CORE (obj);
  grefcount *rc = pw_context_get_user_data (self->pw_context);
  g_return_if_fail (rc);

  wp_core_disconnect (self);

  /* Clear pw-context if refcount reaches 0 */
  if (g_ref_count_dec (rc))
    g_clear_pointer (&self->pw_context, pw_context_destroy);

  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->g_main_context, g_main_context_unref);
  g_clear_pointer (&self->async_tasks, g_hash_table_unref);
  g_clear_object (&self->conf);

  wp_debug_object (self, "WpCore destroyed");

  G_OBJECT_CLASS (wp_core_parent_class)->finalize (obj);
}

static void
wp_core_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpCore *self = WP_CORE (object);

  switch (property_id) {
  case PROP_G_MAIN_CONTEXT:
    g_value_set_boxed (value, self->g_main_context);
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_core_get_properties (self));
    break;
  case PROP_PW_CONTEXT:
    g_value_set_pointer (value, self->pw_context);
    break;
  case PROP_PW_CORE:
    g_value_set_pointer (value, self->pw_core);
    break;
  case PROP_CONF:
    g_value_set_object (value, self->conf);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_core_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpCore *self = WP_CORE (object);

  switch (property_id) {
  case PROP_G_MAIN_CONTEXT:
    self->g_main_context = g_value_dup_boxed (value);
    break;
  case PROP_PROPERTIES:
    self->properties = g_value_dup_boxed (value);
    break;
  case PROP_PW_CONTEXT:
    self->pw_context = g_value_get_pointer (value);
    break;
  case PROP_CONF:
    self->conf = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static WpObjectFeatures
wp_core_get_supported_features (WpObject * self)
{
  return WP_CORE_FEATURE_CONNECTED |
      WP_CORE_FEATURE_COMPONENTS;
}

enum {
  STEP_CONNECT = WP_TRANSITION_STEP_CUSTOM_START,
  STEP_LOAD_COMPONENTS,
};

static guint
wp_core_activate_get_next_step (WpObject * self,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  switch (step) {
    case WP_TRANSITION_STEP_NONE:
      if (missing & WP_CORE_FEATURE_CONNECTED)
        return STEP_CONNECT;
      G_GNUC_FALLTHROUGH;

    case STEP_CONNECT:
      if (missing & WP_CORE_FEATURE_COMPONENTS)
        return STEP_LOAD_COMPONENTS;
      G_GNUC_FALLTHROUGH;

    case STEP_LOAD_COMPONENTS:
      return WP_TRANSITION_STEP_NONE;

    default:
      return WP_TRANSITION_STEP_ERROR;
  }
}

static void
on_components_loaded (WpCore * self, GAsyncResult *res,
    WpTransition * transition)
{
  g_autoptr (GError) error = NULL;

  if (!wp_core_load_component_finish (self, res, &error)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "failed to load components: %s", error->message));
    return;
  }

  if (self->conf) {
    wp_info_object (self, "done loading components, closing conf file...");
    wp_conf_close (self->conf);
  }

  wp_object_update_features (WP_OBJECT (self), WP_CORE_FEATURE_COMPONENTS, 0);
}

static void
wp_core_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpCore *self = WP_CORE (object);

  switch (step) {
    case STEP_CONNECT: {
      wp_info_object (self, "connecting to pipewire...");

      if (!wp_core_connect (self)) {
        wp_transition_return_error (WP_TRANSITION (transition), g_error_new (
            WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_SERVICE_UNAVAILABLE,
            "Failed to connect to PipeWire"));
      }
      break;
    }

    case STEP_LOAD_COMPONENTS: {
      g_autoptr (WpProperties) props = wp_core_get_properties (self);

      if (spa_atob (wp_properties_get (props, "wireplumber.export-core"))) {
        /* do not load any components on the export core */
        wp_object_update_features (WP_OBJECT (self), WP_CORE_FEATURE_COMPONENTS, 0);
        return;
      }
      else {
        const gchar *profile = wp_properties_get (props, "wireplumber.profile");

        wp_info_object (self,
            "parsing & loading components for profile [%s]...", profile);

        /* Load components that are defined in the configuration section */
        wp_core_load_component (self, profile, "profile", NULL, NULL, NULL,
            (GAsyncReadyCallback) on_components_loaded, transition);
      }
      break;
    }

    case WP_TRANSITION_STEP_ERROR:
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
wp_core_deactivate (WpObject * self, WpObjectFeatures features)
{
  if (features & WP_CORE_FEATURE_CONNECTED)
    wp_core_disconnect (WP_CORE (self));

  /* WP_CORE_FEATURE_COMPONENTS cannot be manually deactivated */
}

static void
wp_core_class_init (WpCoreClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->constructed = wp_core_constructed;
  object_class->dispose = wp_core_dispose;
  object_class->finalize = wp_core_finalize;
  object_class->get_property = wp_core_get_property;
  object_class->set_property = wp_core_set_property;

  wpobject_class->get_supported_features = wp_core_get_supported_features;
  wpobject_class->activate_get_next_step = wp_core_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_core_activate_execute_step;
  wpobject_class->deactivate = wp_core_deactivate;

  g_object_class_install_property (object_class, PROP_G_MAIN_CONTEXT,
      g_param_spec_boxed ("g-main-context", "g-main-context",
          "A GMainContext to attach to", G_TYPE_MAIN_CONTEXT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties", "Extra properties",
          WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CONTEXT,
      g_param_spec_pointer ("pw-context", "pw-context", "The pipewire context",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PW_CORE,
      g_param_spec_pointer ("pw-core", "pw-core", "The pipewire core",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CONF,
      g_param_spec_object ("conf", "conf", "The main configuration file",
          WP_TYPE_CONF,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_CONNECTED] = g_signal_new ("connected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  signals[SIGNAL_DISCONNECTED] = g_signal_new ("disconnected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}

/*!
 * \brief Creates a new core object
 *
 * \ingroup wpcore
 * \param context (transfer none) (nullable): the GMainContext to use for events
 * \param conf (transfer full) (nullable): the main configuration file
 * \param properties (transfer full) (nullable): additional properties, which
 *   are also passed to pw_context_new() and pw_context_connect()
 * \returns (transfer full): a new WpCore
 */
WpCore *
wp_core_new (GMainContext * context, WpConf * conf, WpProperties * properties)
{
  g_autoptr (WpConf) c = conf;
  g_autoptr (WpProperties) props = properties;

  return g_object_new (WP_TYPE_CORE,
      "g-main-context", context,
      "conf", conf,
      "properties", properties,
      "pw-context", NULL,
      NULL);
}

/*!
 * \brief Clones a core with the same context as \a self
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full): the clone WpCore
 */
WpCore *
wp_core_clone (WpCore * self)
{
  return g_object_new (WP_TYPE_CORE,
      "core", self,
      "g-main-context", self->g_main_context,
      "conf", self->conf,
      "properties", self->properties,
      "pw-context", self->pw_context,
      NULL);
}

static gboolean
find_export_core (gconstpointer a, gconstpointer b)
{
  gpointer obj = (gpointer) a;
  if (WP_IS_CORE ((gpointer) obj)) {
    g_autoptr (WpProperties) props = wp_core_get_properties (WP_CORE (obj));
    if (spa_atob (wp_properties_get (props, "wireplumber.export-core")))
      return TRUE;
  }
  return FALSE;
}

/*!
 * \brief Returns the special WpCore that is used to maintain a secondary
 * connection to PipeWire, for exporting objects
 *
 * The export core is enabled by loading the built-in "export-core" component.
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full): the export WpCore
 */
WpCore *
wp_core_get_export_core (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  return wp_core_find_object (self, find_export_core, NULL);
}

/*!
 * \brief Gets the main configuration file of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full) (nullable): the main configuration file
 */
WpConf *
wp_core_get_conf (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->conf ? g_object_ref (self->conf) : NULL;
}

/*!
 * \brief Gets the GMainContext of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer none) (nullable): the GMainContext that is in
 *   use by this core for events
 */
GMainContext *
wp_core_get_g_main_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->g_main_context;
}

/*!
 * \brief Gets the internal PipeWire context of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer none): the internal pw_context object
 */
struct pw_context *
wp_core_get_pw_context (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_context;
}

/*!
 * \brief Gets the internal PipeWire core of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer none) (nullable): the internal pw_core object,
 *   or NULL if the core is not connected to PipeWire
 */
struct pw_core *
wp_core_get_pw_core (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  return self->pw_core;
}

/*!
 * \brief Gets the virtual machine type of the core
 *
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full) (nullable): a comma separated string with all the
 *   virtual machine types that this core matches, or NULL if the core is not
 *   running in a virtual machine.
 *
 * \since 0.4.11
 */
gchar *
wp_core_get_vm_type (WpCore *self)
{
  const struct spa_support *support;
  uint32_t n_support;
  struct spa_cpu *spa_cpu;
  uint32_t vm_type;
  gchar *res;
  gboolean first = TRUE;

  struct vm_type_info {
    uint32_t type;
    const gchar *name;
  };

  static struct vm_type_info vm_types[] = {
    {SPA_CPU_VM_OTHER, "other"},
    {SPA_CPU_VM_KVM, "kvm"},
    {SPA_CPU_VM_QEMU, "qemu"},
    {SPA_CPU_VM_BOCHS, "bochs"},
    {SPA_CPU_VM_XEN, "zen"},
    {SPA_CPU_VM_UML, "uml"},
    {SPA_CPU_VM_VMWARE, "vmware"},
    {SPA_CPU_VM_ORACLE, "oracle"},
    {SPA_CPU_VM_MICROSOFT, "microsoft"},
    {SPA_CPU_VM_ZVM, "zvm"},
    {SPA_CPU_VM_PARALLELS, "parallels"},
    {SPA_CPU_VM_BHYVE, "bhyve"},
    {SPA_CPU_VM_QNX, "qnx"},
    {SPA_CPU_VM_ACRN, "acrn"},
    {SPA_CPU_VM_POWERVM, "powervm"},
    {0, NULL},
  };

  g_return_val_if_fail (WP_IS_CORE (self), NULL);
  g_return_val_if_fail (self->pw_context, NULL);

  /* Get spa_cpu */
  support = pw_context_get_support (self->pw_context, &n_support);
  spa_cpu = spa_support_find (support, n_support, SPA_TYPE_INTERFACE_CPU);
  g_return_val_if_fail (spa_cpu, NULL);

  /* Just return NULL if CPU is not a VM */
  vm_type = spa_cpu_get_vm_type (spa_cpu);
  if (vm_type == SPA_CPU_VM_NONE)
    return NULL;

  /* Otherwise return a string with all matching VM types */
  res = g_strdup ("");
  for (guint i = 0; vm_types[i].name; i++) {
    if (vm_type & vm_types[i].type) {
      gchar *tmp = g_strdup_printf ("%s%s%s", res, first ? "": ",",
          vm_types[i].name);
      g_free (res);
      res = tmp;
      first = FALSE;
    }
  }

  return res;
}

/*!
 * \brief Connects this core to the PipeWire server.
 *
 * When connection succeeds, the WpCore \c "connected" signal is emitted.
 *
 * \ingroup wpcore
 * \param self the core
 * \returns TRUE if the core is effectively connected or FALSE if
 *   connection failed
 */
gboolean
wp_core_connect (WpCore *self)
{
  struct pw_properties *p = NULL;

  g_return_val_if_fail (WP_IS_CORE (self), FALSE);

  /* Don't do anything if core is already connected */
  if (self->pw_core)
    return TRUE;

  /* Connect */
  p = self->properties ? wp_properties_to_pw_properties (self->properties) : NULL;
  self->pw_core = pw_context_connect (self->pw_context, p, 0);
  if (!self->pw_core)
    return FALSE;

  /* Add the core listeners */
  pw_core_add_listener (self->pw_core, &self->core_listener, &core_events, self);
  pw_proxy_add_listener((struct pw_proxy*)self->pw_core,
      &self->proxy_core_listener, &proxy_core_events, self);

  /* Add the registry listener */
  wp_registry_attach (&self->registry, self->pw_core);

  return TRUE;
}

/*!
 * \brief Disconnects this core from the PipeWire server.
 *
 * This also effectively destroys all WpCore objects that were created through
 * the registry, destroys the pw_core and finally emits the WpCore
 * \c "disconnected" signal.
 *
 * \ingroup wpcore
 * \param self the core
 */
void
wp_core_disconnect (WpCore *self)
{
  wp_registry_detach (&self->registry);

  /* pw_core_disconnect destroys the core proxy
    and we continue in proxy_core_destroy() */
  if (self->pw_core)
    pw_core_disconnect (self->pw_core);
}

/*!
 * \brief Checks if the core is connected to PipeWire
 *
 * \ingroup wpcore
 * \param self the core
 * \returns TRUE if the core is connected to PipeWire, FALSE otherwise
 */
gboolean
wp_core_is_connected (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  return self->pw_core && self->info;
}

/*!
 * \brief Gets the bound id of the client object that is created as a result
 * of this core being connected to the PipeWire daemon
 *
 * \ingroup wpcore
 * \since 0.4.16
 * \param self the core
 * \returns the bound id of this client
 */
guint32
wp_core_get_own_bound_id (WpCore * self)
{
  struct pw_client *client;

  g_return_val_if_fail (wp_core_is_connected (self), SPA_ID_INVALID);

  client = pw_core_get_client (self->pw_core);
  return pw_proxy_get_bound_id ((struct pw_proxy *) client);
}

/*!
 * \brief Gets the cookie of the core's connected PipeWire instance
 *
 * \ingroup wpcore
 * \param self the core
 * \returns The cookie of the PipeWire instance that \a self is connected to.
 *     The cookie is a unique random number for identifying an instance of
 *     PipeWire
 */
guint32
wp_core_get_remote_cookie (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), 0);

  return self->info->cookie;
}

/*!
 * \brief Gets the name of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns The name of the PipeWire instance that \a self is connected to
 */
const gchar *
wp_core_get_remote_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return self->info->name;
}

/*!
 * \brief Gets the user name of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns The name of the user that started the PipeWire instance that
 *     \a self is connected to
 */
const gchar *
wp_core_get_remote_user_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return self->info->user_name;
}

/*!
 * \brief Gets the host name of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns The name of the host where the PipeWire instance that
 *     \a self is connected to is running on
 */
const gchar *
wp_core_get_remote_host_name (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return self->info->host_name;
}

/*!
 * \brief Gets the version of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns The version of the PipeWire instance that \a self is connected to
 */
const gchar *
wp_core_get_remote_version (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return self->info->version;
}

/*!
 * \brief Gets the properties of the core's connected PipeWire instance
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full): the properties of the PipeWire instance that
 *     \a self is connected to
 */
WpProperties *
wp_core_get_remote_properties (WpCore * self)
{
  g_return_val_if_fail (wp_core_is_connected (self), NULL);

  return wp_properties_new_wrap_dict (self->info->props);
}

/*!
 * \brief Gets the properties of the core
 * \ingroup wpcore
 * \param self the core
 * \returns (transfer full): the properties of \a self
 */
WpProperties *
wp_core_get_properties (WpCore * self)
{
  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  /* pw_core has all the properties of the pw_context,
     plus our updates, passed in pw_context_connect() */
  if (self->pw_core)
    return wp_properties_new_wrap (pw_core_get_properties (self->pw_core));

  /* if there is no connection yet, return the properties of the context */
  else if (!self->properties)
    return wp_properties_new_wrap (pw_context_get_properties (self->pw_context));

  /* ... plus any further updates that we got from wp_core_update_properties() */
  else {
    /* we need to copy here in order to augment with the updates */
    WpProperties *ret =
        wp_properties_new_copy (pw_context_get_properties (self->pw_context));
    wp_properties_update (ret, self->properties);
    return ret;
  }
}

/*!
 * \brief Updates the properties of \a self on the connection, making them
 * appear on the client object that represents this connection.
 *
 * If \a self is not connected yet, these properties are stored and passed to
 * pw_context_connect() when connecting.
 *
 * \ingroup wpcore
 * \param self the core
 * \param updates (transfer full): updates to apply to the properties of
 *    \a self; this does not need to include properties that have not changed
 */
void
wp_core_update_properties (WpCore * self, WpProperties * updates)
{
  g_autoptr (WpProperties) upd = updates;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (updates != NULL);

  /* store updates locally so that
    - they persist after disconnection
    - we can pass them to pw_context_connect */
  if (!self->properties)
    self->properties = wp_properties_new_empty ();
  wp_properties_update (self->properties, upd);

  if (self->pw_core)
    pw_core_update_properties (self->pw_core, wp_properties_peek_dict (upd));
}

/*!
 * \brief Adds an idle callback to be called in the same GMainContext as the
 * one used by this core.
 *
 * This is essentially the same as g_idle_add_full(), but it adds the created
 * GSource on the GMainContext used by this core instead of the default context.
 *
 * \ingroup wpcore
 * \param self the core
 * \param source (out) (optional): the source
 * \param function (scope notified): the function to call
 * \param data (closure): data to pass to \a function
 * \param destroy (nullable): a function to destroy \a data
 */
void
wp_core_idle_add (WpCore * self, GSource **source, GSourceFunc function,
    gpointer data, GDestroyNotify destroy)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));

  s = g_idle_source_new ();
  g_source_set_callback (s, function, data, destroy);
  g_source_attach (s, self->g_main_context);

  if (source)
    *source = g_source_ref (s);
}

/*!
 * \brief Adds an idle callback to be called in the same GMainContext as
 * the one used by this core.
 *
 * This is the same as wp_core_idle_add(), but it allows you to specify a
 * GClosure instead of a C callback.
 *
 * \ingroup wpcore
 * \param self the core
 * \param source (out) (optional): the source
 * \param closure the closure to invoke
 */
void
wp_core_idle_add_closure (WpCore * self, GSource **source, GClosure * closure)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (closure != NULL);

  s = g_idle_source_new ();
  g_source_set_closure (s, closure);
  g_source_attach (s, self->g_main_context);

  if (source)
    *source = g_source_ref (s);
}

/*!
 * \brief Adds a timeout callback to be called at regular intervals in the same
 * GMainContext as the one used by this core.
 *
 * The function is called repeatedly until it returns FALSE, at which point
 * the timeout is automatically destroyed and the function will not be called
 * again. The first call to the function will be at the end of the first
 * interval.

 * This is essentially the same as g_timeout_add_full(), but it adds
 * the created GSource on the GMainContext used by this core instead of the
 * default context.
 *
 * \ingroup wpcore
 * \param self the core
 * \param source (out) (optional): the source
 * \param timeout_ms the timeout in milliseconds
 * \param function (scope notified): the function to call
 * \param data (closure): data to pass to \a function
 * \param destroy (nullable): a function to destroy \a data
 */
void
wp_core_timeout_add (WpCore * self, GSource **source, guint timeout_ms,
    GSourceFunc function, gpointer data, GDestroyNotify destroy)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));

  s = g_timeout_source_new (timeout_ms);
  g_source_set_callback (s, function, data, destroy);
  g_source_attach (s, self->g_main_context);

  if (source)
    *source = g_source_ref (s);
}

/*!
 * \brief Adds a timeout callback to be called at regular intervals in the same
 * GMainContext as the one used by this core.
 *
 * This is the same as wp_core_timeout_add(), but it allows you to specify a
 * GClosure instead of a C callback.
 *
 * \ingroup wpcore
 * \param self the core
 * \param source (out) (optional): the source
 * \param timeout_ms the timeout in milliseconds
 * \param closure the closure to invoke
 */
void
wp_core_timeout_add_closure (WpCore * self, GSource **source, guint timeout_ms,
    GClosure * closure)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (closure != NULL);

  s = g_timeout_source_new (timeout_ms);
  g_source_set_closure (s, closure);
  g_source_attach (s, self->g_main_context);

  if (source)
    *source = g_source_ref (s);
}

/*!
 * \brief Asks the PipeWire server to call the \a callback via an event.
 *
 * Since methods are handled in-order and events are delivered
 * in-order, this can be used as a barrier to ensure all previous
 * methods and the resulting events have been handled.
 *
 * In both success and error cases, \a callback is always called.
 * Use wp_core_sync_finish() from within the \a callback to determine whether
 * the operation completed successfully or if an error occurred.
 *
 * \ingroup wpcore
 * \param self the core
 * \param cancellable (nullable): a GCancellable to cancel the operation
 * \param callback (scope async): a function to call when the operation is done
 * \param user_data (closure): data to pass to \a callback
 * \returns TRUE if the sync operation was started, FALSE if an error
 *   occurred before returning from this function
 */
gboolean
wp_core_sync (WpCore * self, GCancellable * cancellable,
    GAsyncReadyCallback callback, gpointer user_data)
{
  return wp_core_sync_closure (self, cancellable,
      g_cclosure_new (G_CALLBACK (callback), user_data, NULL));
}

static void
invoke_closure (GObject * obj, GAsyncResult * res, gpointer data)
{
  GClosure *closure = data;
  GValue values[2] = { G_VALUE_INIT, G_VALUE_INIT };
  g_value_init (&values[0], G_TYPE_OBJECT);
  g_value_init (&values[1], G_TYPE_OBJECT);
  g_value_set_object (&values[0], obj);
  g_value_set_object (&values[1], res);
  g_closure_invoke (closure, NULL, 2, values, NULL);
  g_value_unset (&values[0]);
  g_value_unset (&values[1]);
  g_closure_unref (closure);
}

/*!
 * \brief Asks the PipeWire server to invoke the \a closure via an event.
 *
 * Since methods are handled in-order and events are delivered
 * in-order, this can be used as a barrier to ensure all previous
 * methods and the resulting events have been handled.
 *
 * In both success and error cases, \a closure is always invoked.
 * Use wp_core_sync_finish() from within the \a closure to determine whether
 * the operation completed successfully or if an error occurred.
 *
 * \ingroup wpcore
 * \since 0.4.6
 * \param self the core
 * \param cancellable (nullable): a GCancellable to cancel the operation
 * \param closure (transfer floating): a closure to invoke when the operation
 *    is done
 * \returns TRUE if the sync operation was started, FALSE if an error
 *   occurred before returning from this function
 */
gboolean
wp_core_sync_closure (WpCore * self, GCancellable * cancellable,
    GClosure * closure)
{
  g_autoptr (GTask) task = NULL;
  int seq;

  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  g_return_val_if_fail (closure, FALSE);

  closure = g_closure_ref (closure);
  g_closure_sink (closure);
  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    g_closure_set_marshal (closure, g_cclosure_marshal_VOID__OBJECT);

  task = g_task_new (self, cancellable, invoke_closure, closure);

  if (G_UNLIKELY (!self->pw_core)) {
    g_warn_if_reached ();
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_INVARIANT, "No pipewire core");
    return FALSE;
  }

  seq = pw_core_sync (self->pw_core, 0, 0);
  if (G_UNLIKELY (seq < 0)) {
    g_task_return_new_error (task, WP_DOMAIN_LIBRARY,
        WP_LIBRARY_ERROR_OPERATION_FAILED, "pw_core_sync failed: %s",
        g_strerror (-seq));
    return FALSE;
  }

  wp_debug_object (self, "sync, seq 0x%x, task " WP_OBJECT_FORMAT,
      seq, WP_OBJECT_ARGS (task));

  g_hash_table_insert (self->async_tasks, GINT_TO_POINTER (seq),
      g_steal_pointer (&task));
  return TRUE;
}

/*!
 * \brief This function is meant to be called from within the callback of
 * wp_core_sync() in order to determine the success or failure of the operation.
 *
 * \ingroup wpcore
 * \param self the core
 * \param res a GAsyncResult
 * \param error (out) (optional): the error that occurred, if any
 * \returns TRUE if the operation succeeded, FALSE otherwise
 */
gboolean
wp_core_sync_finish (WpCore * self, GAsyncResult * res, GError ** error)
{
  g_return_val_if_fail (WP_IS_CORE (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);

  return g_task_propagate_boolean (G_TASK (res), error);
}


/*!
 * \brief Finds a registered object
 *
 * \param self the core
 * \param func (scope call): a function that takes the object being searched
 *   as the first argument and \a data as the second. it should return TRUE if
 *   the object is found or FALSE otherwise
 * \param data the second argument to \a func
 * \returns (transfer full) (type GObject*) (nullable): the registered object
 *   or NULL if not found
 */
gpointer
wp_core_find_object (WpCore * self, GEqualFunc func, gconstpointer data)
{
  GObject *object;
  guint i;

  g_return_val_if_fail (WP_IS_CORE (self), NULL);

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!self->registry.objects))
    return NULL;

  for (i = 0; i < self->registry.objects->len; i++) {
    object = g_ptr_array_index (self->registry.objects, i);
    if (func (object, data))
      return g_object_ref (object);
  }

  return NULL;
}

/*!
 * \brief Registers \a obj with the core, making it appear on WpObjectManager
 * instances as well.
 *
 * The core will also maintain a ref to that object until it
 * is removed.
 *
 * \ingroup wpcore
 * \param self the core
 * \param obj (transfer full) (type GObject*): the object to register
 */
void
wp_core_register_object (WpCore * self, gpointer obj)
{
  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (G_IS_OBJECT (obj));

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!self->registry.objects)) {
    g_object_unref (obj);
    return;
  }

  /* ensure the registered object is associated with this core */
  if (WP_IS_OBJECT (obj)) {
    g_autoptr (WpCore) obj_core = wp_object_get_core (WP_OBJECT (obj));
    g_return_if_fail (obj_core == self);
  }

  g_ptr_array_add (self->registry.objects, obj);

  /* notify object managers */
  wp_registry_notify_add_object (&self->registry, obj);
}

/*!
 * \brief Detaches and unrefs the specified object from this core.
 *
 * \ingroup wpcore
 * \param self the core
 * \param obj (transfer none) (type GObject*): a pointer to the object to remove
 */
void
wp_core_remove_object (WpCore * self, gpointer obj)
{
  g_return_if_fail (WP_IS_CORE (self));
  g_return_if_fail (G_IS_OBJECT (obj));

  /* prevent bad things when called from within wp_registry_clear() */
  if (G_UNLIKELY (!self->registry.objects))
    return;

  /* notify object managers */
  wp_registry_notify_rm_object (&self->registry, obj);

  g_ptr_array_remove_fast (self->registry.objects, obj);
}

/*!
 * \brief Test if a global feature is provided
 *
 * \ingroup wpcore
 * \param self the core
 * \param feature the feature name
 * \returns TRUE if the feature is provided, FALSE otherwise
 */
gboolean
wp_core_test_feature (WpCore * self, const gchar * feature)
{
  return g_ptr_array_find_with_equal_func (self->registry.features, feature,
      g_str_equal, NULL);
}

WpRegistry *
wp_core_get_registry (WpCore * self)
{
  return &self->registry;
}

WpCore *
wp_registry_get_core (WpRegistry * self)
{
  return SPA_CONTAINER_OF (self, WpCore, registry);
}
