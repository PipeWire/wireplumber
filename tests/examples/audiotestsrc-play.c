/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * This is a very simplistic session manager example that also runs an internal
 * PipeWire server for ease of use. The PipeWire server runs in its own thread
 * and our main thread's WpCore (the AppData.core) connects to it through
 * a socket, as if the PipeWire server was in a different process.
 *
 * This example starts 2 media nodes in the media graph: audiotestsrc & alsasink
 * Then, the session management part constructs endpoints for these nodes
 * and links them by creating an endpoint link.
 */

#include <wp/wp.h>
#include <glib-unix.h>
#include "../common/test-server.h"

#define APP_ERROR_DOMAIN (app_error_domain_quark ())
G_DEFINE_QUARK (app-error, app_error_domain)

typedef struct {
  /* our internal test PipeWire server */
  WpTestServer server;

  /* cmdline arguments */
  const gchar *alsa_device;

  /* our main loop and core */
  GMainContext *context;
  GMainLoop *loop;
  WpCore *core;
  WpSession *session;

  /* nodes provider data */
  WpNode *audiotestsrc;
  WpNode *alsasink;

  /* endpoints provider data */
  WpObjectManager *nodes_om;
  GPtrArray *session_items;

  /* policy manager data */
  GSource *interrupt_source;

} AppData;

/*
 * policy manager: link endpoints together
 */

static void
on_endpoints_changed (WpSession * session, AppData * d)
{
  g_autoptr (WpEndpoint) src = NULL;
  g_autoptr (WpEndpoint) sink = NULL;

  g_print ("Endpoints changed, n_endpoints=%u\n",
      wp_session_get_n_endpoints (session));

  /* a very simplistic lookup, since we don't expect any other endpoints
     to show up here, but this is the general idea...
     match endpoints, create links, cache the state and move forward */
  src = wp_session_lookup_endpoint (session,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "=s", "Audio/Source", NULL);
  sink = wp_session_lookup_endpoint (session,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "media.class", "=s", "Audio/Sink", NULL);

  if (src) {
    g_print ("Got endpoint src: %s (%u streams)\n",
      wp_endpoint_get_name (src),
      wp_endpoint_get_n_streams (src));
  }
  if (sink) {
    g_print ("Got endpoint sink: %s (%u streams)\n",
      wp_endpoint_get_name (sink),
      wp_endpoint_get_n_streams (sink));
  }

  if (src && sink) {
    g_autoptr (WpProperties) props = NULL;
    g_autofree gchar * id =
        g_strdup_printf ("%u", wp_proxy_get_bound_id (WP_PROXY (sink)));

    /* only the peer endpoint id is required when linking the default streams;
       everything else will be discovered */
    props = wp_properties_new ("endpoint-link.input.endpoint", id, NULL);
    wp_endpoint_create_link (src, props);
  }
}

static void
on_links_changed (WpSession * session, AppData * d)
{
  guint n_links = wp_session_get_n_links (session);

  /* activate the link - when endpoint links are created,
     they don't do anything unless they are activated first */
  if (n_links == 1) {
    /* lookup with no constraints will just return the first available object */
    g_autoptr (WpEndpointLink) link = wp_session_lookup_link (session, NULL);

    g_print ("Requesting link activation...\n");
    wp_endpoint_link_request_state (link, WP_ENDPOINT_LINK_STATE_ACTIVE);
  }
  else if (n_links == 0) {
    g_print ("Last endpoint link was destroyed; exiting...\n");
    g_main_loop_quit (d->loop);
  }
}

static gboolean
on_interrupted (AppData * d)
{
  g_print ("interrupted; let's try to destroy the link...\n");

  g_autoptr (WpEndpointLink) link = wp_session_lookup_link (d->session, NULL);
  if (link)
    wp_global_proxy_request_destroy (WP_GLOBAL_PROXY (link));

  /* remove the interrupt handler so that we can actually
     interrupt if things get stuck */
  g_clear_pointer (&d->interrupt_source, g_source_unref);
  return G_SOURCE_REMOVE;
}

static void
start_policy_manager (AppData * d)
{
  /* reuse the session pointer that we already have in AppData;
     under other circumstances, we would retrieve the session
     with a WpObjectManager */
  g_signal_connect (d->session, "endpoints-changed",
      G_CALLBACK (on_endpoints_changed), d);
  g_signal_connect (d->session, "links-changed",
      G_CALLBACK (on_links_changed), d);

  d->interrupt_source = g_unix_signal_source_new (SIGINT);
  g_source_set_callback (d->interrupt_source,
      G_SOURCE_FUNC (on_interrupted), d, NULL);
  g_source_attach (d->interrupt_source, d->context);
}

/*
 * endpoints provider: creates endpoints for the discovered nodes
 */

static void
on_si_exported (WpSessionItem * item, GAsyncResult * res, AppData * d)
{
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_export_finish (item, res, &error)) {
    g_printerr ("Failed to export session item: %s\n", error->message);
    g_main_loop_quit (d->loop);
    return;
  }

  g_print ("Item " WP_OBJECT_FORMAT " exported\n", WP_OBJECT_ARGS (item));
}

static void
on_si_activated (WpSessionItem * item, GAsyncResult * res, AppData * d)
{
  g_autoptr (GError) error = NULL;

  if (!wp_session_item_activate_finish (item, res, &error)) {
    g_printerr ("Failed to activate session item: %s\n", error->message);
    g_main_loop_quit (d->loop);
    return;
  }

  g_print ("Item " WP_OBJECT_FORMAT " activated, exporting\n",
      WP_OBJECT_ARGS (item));

  wp_session_item_export (item, d->session,
      (GAsyncReadyCallback) on_si_exported, d);
}

static void
on_node_added (WpObjectManager * om, WpNode *node, AppData * d)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_auto (GVariantBuilder) b = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

  g_print ("Node " WP_OBJECT_FORMAT " added, creating session item\n",
      WP_OBJECT_ARGS (node));

  /* load the "si-adapter" Session Item */
  item = wp_session_item_make (d->core, "si-adapter");

  /* and configure it */
  g_variant_builder_add (&b, "{sv}", "node",
      g_variant_new_uint64 ((guint64) node));
  g_variant_builder_add (&b, "{sv}", "preferred-n-channels",
      g_variant_new_uint32 (2));
  if (!wp_session_item_configure (item, g_variant_builder_end (&b))) {
    g_printerr ("Failed to configure session item\n");
    g_main_loop_quit (d->loop);
    return;
  }

  wp_session_item_activate (item, (GAsyncReadyCallback) on_si_activated, d);
  g_ptr_array_add (d->session_items, g_steal_pointer (&item));
}

static void
start_endpoints_provider (AppData * d)
{
  g_print ("Installing watch for nodes...\n");

  /* register a WpObjectManager to listen for available nodes */
  /* for example purposes, we pretend we don't have access to the data set by
     start_nodes_provider(), i.e. d->audiotestsrc & d->alsasink */
  d->nodes_om = wp_object_manager_new ();
  wp_object_manager_add_interest (d->nodes_om, WP_TYPE_NODE, NULL);
  wp_object_manager_request_object_features (d->nodes_om, WP_TYPE_NODE,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);

  d->session_items = g_ptr_array_new_with_free_func (g_object_unref);

  /* the object manager will emit 'object-added' for every node that is
     made available, once the node has all the features we requested above */
  g_signal_connect (d->nodes_om, "object-added", G_CALLBACK (on_node_added), d);
  wp_core_install_object_manager (d->core, d->nodes_om);
}

/*
 * nodes provider: creates the nodes
 */

static void
on_node_ready (WpObject * node, GAsyncResult * res, AppData * d)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (node, res, &error)) {
    g_printerr ("Failed to prepare node: %s\n", error->message);
    g_main_loop_quit (d->loop);
    return;
  }

  g_print ("Node " WP_OBJECT_FORMAT " is ready\n", WP_OBJECT_ARGS (node));
}

static void
start_nodes_provider (AppData * d)
{
  g_print ("Creating nodes...\n");

  d->audiotestsrc = wp_node_new_from_factory (d->core,
      "adapter", /* the pipewire factory name */
      wp_properties_new (
          /* the spa factory name */
          "factory.name", "audiotestsrc",
          /* a friendly name for our node */
          "node.name", "audiotestsrc",
          NULL));
  g_assert (d->audiotestsrc);
  wp_object_activate (WP_OBJECT (d->audiotestsrc),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL, NULL,
      (GAsyncReadyCallback) on_node_ready, d);

  d->alsasink = wp_node_new_from_factory (d->core,
      "adapter", /* the pipewire factory name */
      wp_properties_new (
          /* the spa factory name */
          "factory.name", "api.alsa.pcm.sink",
          /* a friendly name for our node */
          "node.name", "alsasink",
          /* set the device handle (ex. hw:0,0) on the sink */
          "api.alsa.path", d->alsa_device,
          NULL));
  g_assert (d->alsasink);
  wp_object_activate (WP_OBJECT (d->alsasink),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL, NULL,
      (GAsyncReadyCallback) on_node_ready, d);
}

/*
 * main application: loads modules and the session
 */

static void
on_session_ready (WpObject * session, GAsyncResult * res, AppData * d)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (session, res, &error)) {
    g_printerr ("Failed to prepare session: %s\n", error->message);
    g_main_loop_quit (d->loop);
    return;
  }

  g_print ("Session is ready, starting components...\n");

  start_nodes_provider (d);
  start_endpoints_provider (d);
  start_policy_manager (d);
}

static gboolean
appdata_init (AppData * d, GError ** error)
{
  WpImplSession *session;

  /* setup the internal test PipeWire server */
  wp_test_server_setup (&d->server);
  {
    /* load server modules (pipewire.conf) */
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&d->server);

    pw_context_add_spa_lib (d->server.context,
        "audiotestsrc", "audiotestsrc/libspa-audiotestsrc");
    pw_context_add_spa_lib (d->server.context,
        "api.alsa.*", "alsa/libspa-alsa");

    if (!pw_context_load_module (d->server.context,
            "libpipewire-module-spa-node-factory", NULL, NULL)) {
        g_set_error (error, APP_ERROR_DOMAIN, 0,
            "Failed to load libpipewire-module-spa-node-factory");
        return FALSE;
    }
    if (!pw_context_load_module (d->server.context,
            "libpipewire-module-link-factory", NULL, NULL)) {
        g_set_error (error, APP_ERROR_DOMAIN, 0,
            "Failed to load libpipewire-module-link-factory");
        return FALSE;
    }
    /* adapter is loaded by pw_context */
  }

  /* init our main loop */
  d->context = g_main_context_new ();
  d->loop = g_main_loop_new (d->context, FALSE);

  /* push the context as the thread default for GTask to work with it,
     otherwise it will try to use the "default" main context, which we are
     not using in our main loop, for demonstration purposes */
  g_main_context_push_thread_default (d->context);

  /* init our core; the "remote.name" key tells it to connect to our
     test server instead of the default "pipewire-0" */
  d->core = wp_core_new (d->context, wp_properties_new (
          "remote.name", d->server.name,
          NULL));

  /* load wireplumber modules (wireplumber.conf) */
  if (!(wp_core_load_component (d->core,
          "libwireplumber-module-si-simple-node-endpoint", "module", NULL, error)))
    return FALSE;

  if (!(wp_core_load_component (d->core,
          "libwireplumber-module-si-audio-softdsp-endpoint", "module", NULL, error)))
    return FALSE;

  if (!(wp_core_load_component (d->core,
          "libwireplumber-module-si-adapter", "module", NULL, error)))
    return FALSE;

  if (!(wp_core_load_component (d->core,
          "libwireplumber-module-si-convert", "module", NULL, error)))
    return FALSE;

  if (!(wp_core_load_component (d->core,
          "libwireplumber-module-si-standard-link", "module", NULL, error)))
    return FALSE;

  /* connect */
  if (!wp_core_connect (d->core)) {
    g_set_error (error, APP_ERROR_DOMAIN, 0,
        "Failed to connect to the test server");
    return FALSE;
  }

  g_print ("Creating session...\n");

  /* create a session */
  d->session = WP_SESSION (session = wp_impl_session_new (d->core));
  wp_impl_session_set_property (session, "session.name", "audio");
  wp_object_activate (WP_OBJECT (session), WP_OBJECT_FEATURES_ALL, NULL,
      (GAsyncReadyCallback) on_session_ready, d);
  return TRUE;
}

static void
appdata_clear (AppData * d)
{
  /* policy manager data */
  g_clear_pointer (&d->interrupt_source, g_source_unref);

  /* endpoints provider data */
  g_clear_pointer (&d->session_items, g_ptr_array_unref);
  g_clear_object (&d->nodes_om);

  /* nodes provider data */
  g_clear_object (&d->audiotestsrc);
  g_clear_object (&d->alsasink);

  /* main app data */
  g_clear_object (&d->session);
  g_clear_object (&d->core);
  g_clear_pointer (&d->loop, g_main_loop_unref);
  g_clear_pointer (&d->context, g_main_context_unref);
  wp_test_server_teardown (&d->server);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (AppData, appdata_clear)

gint
main (gint argc, gchar *argv[])
{
  g_auto (AppData) data = {0};
  g_autoptr (GError) error = NULL;

  wp_init (WP_INIT_ALL);

  if (argc > 1)
    data.alsa_device = argv[1];
  else
    data.alsa_device = "hw:0,0";

  if (!appdata_init (&data, &error)) {
    g_printerr ("Initialization failed:\n  %s\n", error->message);
    return 1;
  }

  g_main_loop_run (data.loop);
  return 0;
}
