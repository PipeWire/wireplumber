/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "private/context.h"
#include "core.h"
#include "error.h"
#include "log.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>

#include <spa/support/cpu.h>
#include <spa/utils/result.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-context")

/*! \defgroup wpcontext WpContext */
/*!
 * \struct WpContext
 *
 * The base class of the contexts that WirePlumber runs PipeWire objects in. A
 * context wraps a `pw_context` that runs on a loop, and is associated with the
 * GMainContext of the core.
 *
 * \gproperties
 *
 * \gproperty{core, WpCore *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The core of the context}
 *
 * \gproperty{g-main-context, GMainContext *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The GMainContext of the core}
 *
 * \gproperty{spa-libs, WpSpaJson *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The "context.spa-libs" description of the pw_context}
 *
 * \gproperty{modules, WpSpaJson *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The "context.modules" description of the pw_context}
 *
 * \gproperty{properties, WpProperties *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The properties of the pw_context}
 */
typedef struct _WpContextPrivate WpContextPrivate;
struct _WpContextPrivate
{
  /* Props */
  GWeakRef core;
  GMainContext *g_main_context;
  WpSpaJson *spa_libs;
  WpSpaJson *modules;

  /* Props until they are handed over to the pw_context */
  WpProperties *properties;

  /* Info */
  struct pw_context *pw_context;
};

enum {
  CONTEXT_PROP_0,
  CONTEXT_PROP_CORE,
  CONTEXT_PROP_G_MAIN_CONTEXT,
  CONTEXT_PROP_SPA_LIBS,
  CONTEXT_PROP_MODULES,
  CONTEXT_PROP_PROPERTIES,
  CONTEXT_N_PROPS,
};

static GParamSpec *context_props[CONTEXT_N_PROPS] = { NULL, };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpContext, wp_context, G_TYPE_OBJECT)

static void
wp_context_init (WpContext * self)
{
  WpContextPrivate *priv = wp_context_get_instance_private (self);

  g_weak_ref_init (&priv->core, NULL);
}

static void
wp_context_constructed (GObject * object)
{
  WpContext *self = WP_CONTEXT (object);
  WpContextPrivate *priv = wp_context_get_instance_private (self);
  WpContextClass *klass = WP_CONTEXT_GET_CLASS (self);
  struct pw_loop *loop;
  struct pw_properties *props = NULL;

  G_OBJECT_CLASS (wp_context_parent_class)->constructed (object);

  /* get the pipewire loop */
  g_return_if_fail (klass->get_pw_loop);
  loop = klass->get_pw_loop (self);
  g_return_if_fail (loop);

  /* the properties are fully stored in the pw_context, no need to keep them */
  if (priv->properties)
    props = wp_properties_unref_and_take_pw_properties (
        g_steal_pointer (&priv->properties));

  /* create the pipewire context */
  priv->pw_context = pw_context_new (loop, g_steal_pointer (&props), 0);
  g_return_if_fail (priv->pw_context);
}

static void
wp_context_dispose (GObject * object)
{
  WpContextPrivate *priv =
      wp_context_get_instance_private (WP_CONTEXT (object));

  /* subclasses destroy the loop in finalize() */
  g_clear_pointer (&priv->pw_context, pw_context_destroy);

  G_OBJECT_CLASS (wp_context_parent_class)->dispose (object);
}

static void
wp_context_finalize (GObject * object)
{
  WpContextPrivate *priv =
      wp_context_get_instance_private (WP_CONTEXT (object));

  g_clear_pointer (&priv->spa_libs, wp_spa_json_unref);
  g_clear_pointer (&priv->modules, wp_spa_json_unref);
  g_clear_pointer (&priv->properties, wp_properties_unref);
  g_clear_pointer (&priv->g_main_context, g_main_context_unref);
  g_weak_ref_clear (&priv->core);

  G_OBJECT_CLASS (wp_context_parent_class)->finalize (object);
}

static void
wp_context_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpContextPrivate *priv =
      wp_context_get_instance_private (WP_CONTEXT (object));

  switch (property_id) {
  case CONTEXT_PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&priv->core));
    break;
  case CONTEXT_PROP_G_MAIN_CONTEXT:
    g_value_set_boxed (value, priv->g_main_context);
    break;
  case CONTEXT_PROP_SPA_LIBS:
    g_value_set_boxed (value, priv->spa_libs);
    break;
  case CONTEXT_PROP_MODULES:
    g_value_set_boxed (value, priv->modules);
    break;
  case CONTEXT_PROP_PROPERTIES:
    g_value_take_boxed (value, priv->pw_context ?
        wp_context_get_properties (WP_CONTEXT (object)) : NULL);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_context_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpContextPrivate *priv =
      wp_context_get_instance_private (WP_CONTEXT (object));

  switch (property_id) {
  case CONTEXT_PROP_CORE:
    g_weak_ref_set (&priv->core, g_value_get_object (value));
    break;
  case CONTEXT_PROP_G_MAIN_CONTEXT:
    priv->g_main_context = g_value_dup_boxed (value);
    break;
  case CONTEXT_PROP_SPA_LIBS:
    priv->spa_libs = g_value_dup_boxed (value);
    break;
  case CONTEXT_PROP_MODULES:
    priv->modules = g_value_dup_boxed (value);
    break;
  case CONTEXT_PROP_PROPERTIES:
    priv->properties = g_value_dup_boxed (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_context_class_init (WpContextClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->constructed = wp_context_constructed;
  object_class->dispose = wp_context_dispose;
  object_class->finalize = wp_context_finalize;
  object_class->get_property = wp_context_get_property;
  object_class->set_property = wp_context_set_property;

  context_props[CONTEXT_PROP_CORE] = g_param_spec_object (
      "core", "core", "The core of the context", WP_TYPE_CORE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  context_props[CONTEXT_PROP_G_MAIN_CONTEXT] = g_param_spec_boxed (
      "g-main-context", "g-main-context", "The GMainContext of the core",
      G_TYPE_MAIN_CONTEXT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  context_props[CONTEXT_PROP_SPA_LIBS] = g_param_spec_boxed (
      "spa-libs", "spa-libs",
      "The \"context.spa-libs\" description of the pw_context",
      WP_TYPE_SPA_JSON,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  context_props[CONTEXT_PROP_MODULES] = g_param_spec_boxed (
      "modules", "modules",
      "The \"context.modules\" description of the pw_context",
      WP_TYPE_SPA_JSON,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  context_props[CONTEXT_PROP_PROPERTIES] = g_param_spec_boxed (
      "properties", "properties", "The properties of the pw_context",
      WP_TYPE_PROPERTIES,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, CONTEXT_N_PROPS,
      context_props);
}

#include "private/parse-conf-section.c"

static gboolean
wp_context_load (WpContext * self, GError ** error)
{
  WpContextPrivate *priv = wp_context_get_instance_private (self);
  gint res;
  WpProperties *conf_wp;
  struct pw_properties *conf_pw;

  g_return_val_if_fail (priv->pw_context, FALSE);

  /* convert the sections into a pipewire-style conf dictionary */
  conf_wp = wp_properties_new_empty ();
  if (priv->spa_libs) {
    g_autofree gchar *js = wp_spa_json_parse_string (priv->spa_libs);
    wp_properties_set (conf_wp, "context.spa-libs", js);
  }
  if (priv->modules) {
    g_autofree gchar *js = wp_spa_json_parse_string (priv->modules);
    wp_properties_set (conf_wp, "context.modules", js);
  }
  conf_pw = wp_properties_unref_and_take_pw_properties (conf_wp);

  if (priv->spa_libs) {
    if ((res = _pw_context_parse_conf_section (priv->pw_context, conf_pw,
                "context.spa-libs")) < 0)
      goto error;
    wp_info_object (self, "parsed %d context.spa-libs items", res);
  }

  if (priv->modules) {
    if ((res = _pw_context_parse_conf_section (priv->pw_context, conf_pw,
                "context.modules")) < 0)
      goto error;
    wp_info_object (self, "parsed %d context.modules items", res);
  }

  pw_properties_free (conf_pw);
  return TRUE;

error:
  pw_properties_free (conf_pw);
  g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
      "failed to parse pw_context sections: %s", spa_strerror (res));
  return FALSE;
}

/*!
 * \brief Gets the GMainContext of the context
 *
 * \ingroup wpcontext
 * \param self the context
 * \returns (transfer none) (nullable): the GMainContext that is in
 *   use by this context for events
 */
GMainContext *
wp_context_get_g_main_context (WpContext * self)
{
  g_return_val_if_fail (WP_IS_CONTEXT (self), NULL);

  WpContextPrivate *priv = wp_context_get_instance_private (self);
  return priv->g_main_context;
}

/*!
 * \brief Gets the "context.spa-libs" description of the context
 *
 * \ingroup wpcontext
 * \param self the context
 * \returns (transfer none) (nullable): the "context.spa-libs" description of
 *   \a self
 */
WpSpaJson *
wp_context_get_spa_libs (WpContext * self)
{
  g_return_val_if_fail (WP_IS_CONTEXT (self), NULL);

  WpContextPrivate *priv = wp_context_get_instance_private (self);
  return priv->spa_libs;
}

/*!
 * \brief Gets the "context.modules" description of the context
 *
 * \ingroup wpcontext
 * \param self the context
 * \returns (transfer none) (nullable): the "context.modules" description of
 *   \a self
 */
WpSpaJson *
wp_context_get_modules (WpContext * self)
{
  g_return_val_if_fail (WP_IS_CONTEXT (self), NULL);

  WpContextPrivate *priv = wp_context_get_instance_private (self);
  return priv->modules;
}

/*!
 * \brief Gets the properties of the context
 *
 * \ingroup wpcontext
 * \param self the context
 * \returns (transfer full): the properties of \a self
 */
WpProperties *
wp_context_get_properties (WpContext * self)
{
  struct pw_context *pw_context;

  g_return_val_if_fail (WP_IS_CONTEXT (self), NULL);

  pw_context = wp_context_get_pw_context (self);
  g_return_val_if_fail (pw_context, NULL);

  return wp_properties_new_wrap (pw_context_get_properties (pw_context));
}

/*!
 * \brief Gets the internal PipeWire context
 *
 * \ingroup wpcontext
 * \param self the context
 * \returns (transfer none): the internal pw_context object
 */
struct pw_context *
wp_context_get_pw_context (WpContext * self)
{
  g_return_val_if_fail (WP_IS_CONTEXT (self), NULL);

  WpContextPrivate *priv = wp_context_get_instance_private (self);
  return priv->pw_context;
}

/*!
 * \brief Gets the virtual machine type of the context
 *
 * \ingroup wpcontext
 * \param self the context
 * \returns (transfer full) (nullable): a comma separated string with all the
 *   virtual machine types that this context matches, or NULL if the context is
 *   not running in a virtual machine. VM types that are not known to
 *   WirePlumber are reported as "unknown".
 */
gchar *
wp_context_get_vm_type (WpContext * self)
{
  struct pw_context *pw_context;
  const struct spa_support *support;
  uint32_t n_support;
  struct spa_cpu *spa_cpu;
  uint32_t vm_type;
  uint32_t matched = 0;
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
    {SPA_CPU_VM_XEN, "xen"},
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

  g_return_val_if_fail (WP_IS_CONTEXT (self), NULL);
  pw_context = wp_context_get_pw_context (self);
  g_return_val_if_fail (pw_context, NULL);

  /* Get spa_cpu */
  support = pw_context_get_support (pw_context, &n_support);
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
      matched |= vm_types[i].type;
      gchar *tmp = g_strdup_printf ("%s%s%s", res, first ? "": ",",
          vm_types[i].name);
      g_free (res);
      res = tmp;
      first = FALSE;
    }
  }

  /* Some VM types were not in the table above */
  if (matched != vm_type) {
    gchar *tmp = g_strdup_printf ("%s%sunknown", res, first ? "": ",");
    g_free (res);
    res = tmp;
  }

  return res;
}

/*!
 * \brief Adds an idle callback to be called in the same GMainContext as the
 * one used by this context.
 *
 * This is essentially the same as g_idle_add_full(), but it adds the created
 * GSource on the GMainContext used by this context instead of the default
 * context.
 *
 * \ingroup wpcontext
 * \param self the context
 * \param source (out) (optional): the source
 * \param function (scope notified)(closure data)(destroy destroy): the function to call
 * \param data data to pass to \a function
 * \param destroy (nullable): a function to destroy \a data
 */
void
wp_context_idle_add (WpContext * self, GSource **source,
    GSourceFunc function,
    gpointer data, GDestroyNotify destroy)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CONTEXT (self));

  s = g_idle_source_new ();
  g_source_set_callback (s, function, data, destroy);
  g_source_attach (s, wp_context_get_g_main_context (self));

  if (source)
    *source = g_source_ref (s);
}

/*!
 * \brief Adds an idle callback to be called in the same GMainContext as
 * the one used by this context.
 *
 * This is the same as wp_context_idle_add(), but it allows you to specify
 * a GClosure instead of a C callback.
 *
 * \ingroup wpcontext
 * \param self the context
 * \param source (out) (optional): the source
 * \param closure the closure to invoke
 */
void
wp_context_idle_add_closure (WpContext * self, GSource **source,
    GClosure * closure)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CONTEXT (self));
  g_return_if_fail (closure != NULL);

  s = g_idle_source_new ();
  g_source_set_closure (s, closure);
  g_source_attach (s, wp_context_get_g_main_context (self));

  if (source)
    *source = g_source_ref (s);
}

/*!
 * \brief Adds a timeout callback to be called at regular intervals in the same
 * GMainContext as the one used by this context.
 *
 * The function is called repeatedly until it returns FALSE, at which point
 * the timeout is automatically destroyed and the function will not be called
 * again. The first call to the function will be at the end of the first
 * interval.
 *
 * This is essentially the same as g_timeout_add_full(), but it adds
 * the created GSource on the GMainContext used by this context instead of the
 * default context.
 *
 * \ingroup wpcontext
 * \param self the context
 * \param source (out) (optional): the source
 * \param timeout_ms the timeout in milliseconds
 * \param function (scope notified)(closure data)(destroy destroy): the function to call
 * \param data data to pass to \a function
 * \param destroy (nullable): a function to destroy \a data
 */
void
wp_context_timeout_add (WpContext * self, GSource **source,
    guint timeout_ms,
    GSourceFunc function, gpointer data, GDestroyNotify destroy)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CONTEXT (self));

  s = g_timeout_source_new (timeout_ms);
  g_source_set_callback (s, function, data, destroy);
  g_source_attach (s, wp_context_get_g_main_context (self));

  if (source)
    *source = g_source_ref (s);
}

/*!
 * \brief Adds a timeout callback to be called at regular intervals in the same
 * GMainContext as the one used by this context.
 *
 * This is the same as wp_context_timeout_add(), but it allows you to
 * specify a GClosure instead of a C callback.
 *
 * \ingroup wpcontext
 * \param self the context
 * \param source (out) (optional): the source
 * \param timeout_ms the timeout in milliseconds
 * \param closure the closure to invoke
 */
void
wp_context_timeout_add_closure (WpContext * self, GSource **source,
    guint timeout_ms, GClosure * closure)
{
  g_autoptr (GSource) s = NULL;

  g_return_if_fail (WP_IS_CONTEXT (self));
  g_return_if_fail (closure != NULL);

  s = g_timeout_source_new (timeout_ms);
  g_source_set_closure (s, closure);
  g_source_attach (s, wp_context_get_g_main_context (self));

  if (source)
    *source = g_source_ref (s);
}

/*!
 * \brief Gets the core of the context
 *
 * \ingroup wpcontext
 * \param self the context
 * \returns (transfer full) (nullable): the core of \a self, or NULL if that
 *   core is gone
 */
WpCore *
wp_context_get_core (WpContext * self)
{
  g_return_val_if_fail (WP_IS_CONTEXT (self), NULL);

  WpContextPrivate *priv = wp_context_get_instance_private (self);
  return g_weak_ref_get (&priv->core);
}


/* WpMainContext */

/* Integration between the PipeWire main loop and GMainLoop */
#define WP_LOOP_SOURCE(x) ((WpLoopSource *) x)

typedef struct _WpLoopSource WpLoopSource;
struct _WpLoopSource
{
  GSource parent;
  struct pw_loop *loop;
  gboolean entered;
};

static gboolean
wp_loop_source_dispatch (GSource * s, GSourceFunc callback, gpointer user_data)
{
  WpLoopSource *ls = WP_LOOP_SOURCE (s);
  int result;

  if (!ls->entered) {
    wp_trace_boxed (G_TYPE_SOURCE, s, "entering pw main loop");
    pw_loop_enter (ls->loop);
    ls->entered = TRUE;
    g_source_set_ready_time (s, -1);
  }

  result = pw_loop_iterate (ls->loop, 0);

  if (G_UNLIKELY (result < 0))
    wp_warning_boxed (G_TYPE_SOURCE, s,
        "pw_loop_iterate failed: %s", spa_strerror (result));

  return G_SOURCE_CONTINUE;
}

static void
wp_loop_source_finalize (GSource * s)
{
  WpLoopSource *ls = WP_LOOP_SOURCE (s);

  wp_trace_boxed (G_TYPE_SOURCE, s, "finalize loop source");

  /* Source should be left from the thread it was entered from.
   *
   * This puts additional restrictions to upper layers on how WpLoopSource (and
   * WpMainContext) can be used: they must be finalized from the GMainContext
   * thread.
   */
  if (ls->entered) {
    wp_trace_boxed (G_TYPE_SOURCE, s, "leaving pw main loop");
    pw_loop_leave (ls->loop);
  }

  pw_loop_destroy (ls->loop);
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

  /* dispatch immediately to enter the loop */
  g_source_set_ready_time (s, 0);

  return (GSource *) s;
}

/*!
 * \struct WpMainContext
 *
 * The context wraps a `pw_context` and glues the PipeWire library's event loop
 * system with GMainLoop. It is created by wp_core_new().
 */
struct _WpMainContext
{
  WpContext parent;

  /* main loop integration */
  GSource *loop_source;
};

G_DEFINE_TYPE (WpMainContext, wp_main_context, WP_TYPE_CONTEXT)

static void
wp_main_context_init (WpMainContext * self)
{
}

static struct pw_loop *
wp_main_context_get_pw_loop (WpContext * ctx)
{
  return WP_LOOP_SOURCE (WP_MAIN_CONTEXT (ctx)->loop_source)->loop;
}

static void
wp_main_context_constructed (GObject *object)
{
  WpMainContext *self = WP_MAIN_CONTEXT (object);
  g_autoptr (GError) error = NULL;

  self->loop_source = wp_loop_source_new ();

  /* this creates the pw_context */
  G_OBJECT_CLASS (wp_main_context_parent_class)->constructed (object);

  if (!wp_context_load (WP_CONTEXT (self), &error))
    wp_critical_object (self, "%s", error->message);

  /* Start source */
  g_source_attach (self->loop_source,
      wp_context_get_g_main_context (WP_CONTEXT (self)));
}

static void
wp_main_context_finalize (GObject * obj)
{
  WpMainContext *self = WP_MAIN_CONTEXT (obj);

  if (self->loop_source)
    g_source_destroy (self->loop_source);
  g_clear_pointer (&self->loop_source, g_source_unref);

  G_OBJECT_CLASS (wp_main_context_parent_class)->finalize (obj);
}

static void
wp_main_context_class_init (WpMainContextClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpContextClass *context_class = (WpContextClass *) klass;

  context_class->get_pw_loop = wp_main_context_get_pw_loop;

  object_class->constructed = wp_main_context_constructed;
  object_class->finalize = wp_main_context_finalize;
}

/* Created by the core; g_main_context is (transfer none) and the other
   arguments are (transfer full), all of them (nullable) */
WpMainContext *
wp_main_context_new (WpCore * core, GMainContext * g_main_context,
    WpProperties * properties, WpSpaJson * spa_libs, WpSpaJson * modules)
{
  g_autoptr (WpProperties) props = properties;
  g_autoptr (WpSpaJson) sl = spa_libs;
  g_autoptr (WpSpaJson) m = modules;

  return g_object_new (WP_TYPE_MAIN_CONTEXT,
      "core", core,
      "g-main-context", g_main_context,
      "spa-libs", sl,
      "modules", m,
      "properties", props,
      NULL);
}
