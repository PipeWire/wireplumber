/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "export-context.h"
#include "conf-sections.h"
#include "../error.h"
#include "../log.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>
#include <pipewire/thread-loop.h>

#include <spa/utils/result.h>

#include <errno.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-export-context")

/*
 * The export pw_context never loads a .conf file of its own (see
 * build_properties()); it is configured only from the component's "arguments",
 * falling back to the main WpConf and then to the lists below.
 *
 * The spa factory mappings that the modules below need, used when neither the
 * component arguments nor the main WpConf provide a "context.spa-libs" section.
 * This is the same set that pipewire's client.conf has.
 */
#define DEFAULT_CONTEXT_SPA_LIBS \
  "{" \
  "  audio.convert.* = audioconvert/libspa-audioconvert" \
  "  support.*       = support/libspa-support" \
  "}"

/*
 * The modules that the export context needs in order to be useful, used when
 * the component does not provide a "context.modules" argument. Note that
 * modules are per-context, so these are loaded in addition to whatever the
 * main context has loaded:
 *  - protocol-native: required to connect at all
 *  - client-node: required by pw_core_export(), which is how pw_stream and
 *    pw_filter (and therefore module-loopback, module-filter-chain, ...)
 *    publish their local pw_impl_node
 *  - client-device: same, for the spa_device handles behind SpaDevice()
 *  - adapter: pw_stream_connect() wraps its node in an adapter, and it is also
 *    the factory behind LocalNode("adapter")
 *  - spa-node-factory: the factory behind LocalNode("spa-node-factory")
 *  - metadata: needed by modules that watch or set metadata
 *  - rt: RT priority for this context's data loop
 *
 * This is essentially the client side of pipewire's own client.conf, which is
 * what these modules would get if they ran in a process of their own.
 */
#define DEFAULT_CONTEXT_MODULES \
  "[" \
  "  { name = libpipewire-module-rt, flags = [ ifexists, nofail ] }" \
  "  { name = libpipewire-module-protocol-native }" \
  "  { name = libpipewire-module-client-node }" \
  "  { name = libpipewire-module-client-device }" \
  "  { name = libpipewire-module-adapter }" \
  "  { name = libpipewire-module-spa-node-factory }" \
  "  { name = libpipewire-module-metadata }" \
  "]"

enum {
  SIGNAL_DISCONNECTED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

/*** cross-thread invoke queue ***/

typedef struct {
  GSourceFunc callback;
  gpointer data;
  GDestroyNotify destroy;
} InvokeItem;

static void
invoke_item_free (InvokeItem * item)
{
  if (item->destroy)
    item->destroy (item->data);
  g_free (item);
}

struct _WpExportContext
{
  GObject parent;

  GWeakRef core;
  GMainContext *g_main_context;

  struct pw_thread_loop *thread_loop;
  struct pw_context *pw_context;
  struct pw_core *pw_core;
  struct spa_hook core_listener;
  struct spa_hook proxy_core_listener;

  /* queue of calls to be dispatched on the main thread;
     protected by queue_lock, which may be taken from either thread */
  GMutex queue_lock;
  GQueue queue;
  GSource *queue_source;
  gboolean stopping;
};

G_DEFINE_TYPE (WpExportContext, wp_export_context, G_TYPE_OBJECT)

typedef struct {
  GSource parent;
  WpExportContext *self;
} QueueSource;

static gboolean
queue_source_has_items (QueueSource * qs)
{
  WpExportContext *self = qs->self;
  gboolean ready;

  g_mutex_lock (&self->queue_lock);
  ready = !g_queue_is_empty (&self->queue);
  g_mutex_unlock (&self->queue_lock);

  return ready;
}

static gboolean
queue_source_prepare (GSource * s, gint * timeout)
{
  *timeout = -1;
  return queue_source_has_items ((QueueSource *) s);
}

static gboolean
queue_source_check (GSource * s)
{
  return queue_source_has_items ((QueueSource *) s);
}

static gboolean
queue_source_dispatch (GSource * s, GSourceFunc callback, gpointer user_data)
{
  WpExportContext *self = ((QueueSource *) s)->self;
  InvokeItem *item;

  /* dispatch a single item, so that other sources get a chance to run
     in between; if there are more, prepare() will return TRUE again */
  g_mutex_lock (&self->queue_lock);
  item = g_queue_pop_head (&self->queue);
  g_mutex_unlock (&self->queue_lock);

  if (item) {
    item->callback (item->data);
    invoke_item_free (item);
  }

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs queue_source_funcs = {
  queue_source_prepare,
  queue_source_check,
  queue_source_dispatch,
  NULL
};

void
wp_export_context_invoke_main (WpExportContext * self, GSourceFunc callback,
    gpointer data, GDestroyNotify destroy)
{
  InvokeItem *item;

  g_return_if_fail (WP_IS_EXPORT_CONTEXT (self));
  g_return_if_fail (callback);

  item = g_new0 (InvokeItem, 1);
  item->callback = callback;
  item->data = data;
  item->destroy = destroy;

  g_mutex_lock (&self->queue_lock);
  if (G_UNLIKELY (self->stopping)) {
    g_mutex_unlock (&self->queue_lock);
    invoke_item_free (item);
    return;
  }
  g_queue_push_tail (&self->queue, item);
  g_mutex_unlock (&self->queue_lock);

  g_main_context_wakeup (self->g_main_context);
}

/*** connection monitoring ***/

static gboolean
on_disconnected_idle (gpointer data)
{
  WpExportContext *self = WP_EXPORT_CONTEXT (data);
  g_signal_emit (self, signals[SIGNAL_DISCONNECTED], 0);
  return G_SOURCE_REMOVE;
}

/* called on the loop thread */
static void
core_error (void *data, uint32_t id, int seq, int res, const char *message)
{
  WpExportContext *self = WP_EXPORT_CONTEXT (data);

  if (id == 0) {
    wp_warning_object (self, "export context connection error: %s", message);
    if (res == -EPIPE)
      wp_export_context_invoke_main (self, on_disconnected_idle,
          g_object_ref (self), g_object_unref);
  }
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .error = core_error,
};

/* called on the loop thread */
static void
proxy_core_destroy (void *data)
{
  WpExportContext *self = WP_EXPORT_CONTEXT (data);

  spa_hook_remove (&self->core_listener);
  spa_hook_remove (&self->proxy_core_listener);
  self->pw_core = NULL;
}

static const struct pw_proxy_events proxy_core_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = proxy_core_destroy,
};

/*** construction ***/

static struct pw_properties *
build_properties (WpCore * core, WpSpaJson * args)
{
  g_autoptr (WpProperties) props = wp_core_get_properties (core);

  /* we are going to modify these */
  props = wp_properties_ensure_unique_owner (g_steal_pointer (&props));

  /* properties that describe the main core's connection specifically */
  wp_properties_set (props, PW_KEY_CORE_NAME, NULL);

  /* mark this connection, so that it can be told apart in pw-cli & friends */
  wp_properties_set (props, "wireplumber.export-context", "true");

  /* This context is configured entirely from load_modules() below, so keep
     pw_context_new() from loading pipewire's client.conf on top of that.
     "null" is pipewire's documented way to disable loading a .conf file;
     allow-empty then silences the warning about the (still empty at that
     point) context.modules section. */
  wp_properties_set (props, PW_KEY_CONFIG_NAME, "null");
  wp_properties_set (props, "context.modules.allow-empty", "true");

  if (args) {
    g_autoptr (WpSpaJson) j = NULL;
    if (wp_spa_json_object_get (args, "context.properties", "J", &j, NULL))
      wp_properties_update_from_json (props, j);
  }

  return wp_properties_unref_and_take_pw_properties (g_steal_pointer (&props));
}

static gboolean
load_modules (WpExportContext * self, WpCore * core, WpSpaJson * args,
    GError ** error)
{
  g_autoptr (WpConf) conf = wp_core_get_conf (core);
  g_autoptr (WpSpaJson) spa_libs = NULL;
  g_autoptr (WpSpaJson) modules = NULL;
  gint res;

  if (args) {
    wp_spa_json_object_get (args, "context.spa-libs", "J", &spa_libs, NULL);
    wp_spa_json_object_get (args, "context.modules", "J", &modules, NULL);
  }

  /* the export context needs the same spa factory mappings as the main one */
  if (!spa_libs && conf)
    spa_libs = wp_conf_get_section (conf, "context.spa-libs");
  if (!spa_libs)
    spa_libs = wp_spa_json_new_from_string (DEFAULT_CONTEXT_SPA_LIBS);

  if (!modules)
    modules = wp_spa_json_new_from_string (DEFAULT_CONTEXT_MODULES);

  res = wp_pw_context_apply_conf_sections (self, self->pw_context, spa_libs,
      modules, error);
  if (res < 0)
    return FALSE;

  wp_info_object (self, "loaded %d modules in the export context", res);
  return TRUE;
}

WpExportContext *
wp_export_context_new (WpCore * core, WpSpaJson * args, GError ** error)
{
  g_autoptr (WpExportContext) self = NULL;
  struct pw_properties *props = NULL;

  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  self = g_object_new (WP_TYPE_EXPORT_CONTEXT, NULL);
  g_weak_ref_set (&self->core, core);
  self->g_main_context =
      g_main_context_ref (wp_core_get_g_main_context (core) ?
          wp_core_get_g_main_context (core) : g_main_context_default ());

  self->thread_loop = pw_thread_loop_new ("wp-export", NULL);
  if (!self->thread_loop) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to create the export context thread loop");
    return NULL;
  }

  props = build_properties (core, args);
  self->pw_context =
      pw_context_new (pw_thread_loop_get_loop (self->thread_loop), props, 0);
  if (!self->pw_context) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to create the export pw_context");
    return NULL;
  }

  /* everything below runs before the loop thread is started,
     so no locking is needed yet */

  if (!load_modules (self, core, args, error))
    return NULL;

  self->pw_core = pw_context_connect (self->pw_context, NULL, 0);
  if (!self->pw_core) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_SERVICE_UNAVAILABLE,
        "failed to connect the export context to PipeWire: %s",
        g_strerror (errno));
    return NULL;
  }
  pw_core_add_listener (self->pw_core, &self->core_listener, &core_events, self);
  pw_proxy_add_listener ((struct pw_proxy *) self->pw_core,
      &self->proxy_core_listener, &proxy_core_events, self);

  self->queue_source = g_source_new (&queue_source_funcs, sizeof (QueueSource));
  ((QueueSource *) self->queue_source)->self = self;
  g_source_set_priority (self->queue_source, G_PRIORITY_DEFAULT);
  g_source_attach (self->queue_source, self->g_main_context);

  if (pw_thread_loop_start (self->thread_loop) < 0) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to start the export context thread loop");
    return NULL;
  }

  wp_info_object (self, "export context started");
  return g_steal_pointer (&self);
}

static gboolean
find_export_context (gconstpointer a, gconstpointer b)
{
  return WP_IS_EXPORT_CONTEXT ((gpointer)a);
}

WpExportContext *
wp_export_context_find (WpCore * core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  GObject *c = wp_core_find_object (core, find_export_context, NULL);
  return c ? WP_EXPORT_CONTEXT (c) : NULL;
}

/*** accessors ***/

void
wp_export_context_lock (WpExportContext * self)
{
  g_return_if_fail (WP_IS_EXPORT_CONTEXT (self));
  pw_thread_loop_lock (self->thread_loop);
}

void
wp_export_context_unlock (WpExportContext * self)
{
  g_return_if_fail (WP_IS_EXPORT_CONTEXT (self));
  pw_thread_loop_unlock (self->thread_loop);
}

gboolean
wp_export_context_in_thread (WpExportContext * self)
{
  g_return_val_if_fail (WP_IS_EXPORT_CONTEXT (self), FALSE);
  return pw_thread_loop_in_thread (self->thread_loop);
}

struct pw_context *
wp_export_context_get_pw_context (WpExportContext * self)
{
  g_return_val_if_fail (WP_IS_EXPORT_CONTEXT (self), NULL);
  return self->pw_context;
}

struct pw_core *
wp_export_context_get_pw_core (WpExportContext * self)
{
  g_return_val_if_fail (WP_IS_EXPORT_CONTEXT (self), NULL);
  return self->pw_core;
}

/*** GObject ***/

static void
wp_export_context_init (WpExportContext * self)
{
  g_weak_ref_init (&self->core, NULL);
  g_mutex_init (&self->queue_lock);
  g_queue_init (&self->queue);
}

static void
wp_export_context_finalize (GObject * object)
{
  WpExportContext *self = WP_EXPORT_CONTEXT (object);
  InvokeItem *item;

  /* stop the loop thread first; from here on, nothing can be queued anymore
     and no pw/spa callback can run */
  if (self->thread_loop)
    pw_thread_loop_stop (self->thread_loop);

  g_mutex_lock (&self->queue_lock);
  self->stopping = TRUE;
  g_mutex_unlock (&self->queue_lock);

  if (self->queue_source) {
    g_source_destroy (self->queue_source);
    g_clear_pointer (&self->queue_source, g_source_unref);
  }

  while ((item = g_queue_pop_head (&self->queue)))
    invoke_item_free (item);

  if (self->pw_core)
    pw_core_disconnect (self->pw_core);
  g_clear_pointer (&self->pw_context, pw_context_destroy);
  g_clear_pointer (&self->thread_loop, pw_thread_loop_destroy);

  g_clear_pointer (&self->g_main_context, g_main_context_unref);
  g_mutex_clear (&self->queue_lock);
  g_weak_ref_clear (&self->core);

  wp_debug_object (self, "export context destroyed");

  G_OBJECT_CLASS (wp_export_context_parent_class)->finalize (object);
}

static void
wp_export_context_class_init (WpExportContextClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_export_context_finalize;

  signals[SIGNAL_DISCONNECTED] = g_signal_new ("disconnected",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);
}
