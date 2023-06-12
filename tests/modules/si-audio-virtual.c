/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
} TestFixture;

static void
on_plugin_loaded (WpCore * core, GAsyncResult * res, TestFixture *f)
{
  gboolean loaded;
  GError *error = NULL;

  loaded = wp_core_load_component_finish (core, res, &error);
  g_assert_no_error (error);
  g_assert_true (loaded);

  g_main_loop_quit (f->base.loop);
}

static void
test_si_audio_virtual_setup (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&f->base, 0);

  /* load modules */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-spa-node-factory", NULL, NULL));
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
  }
  {
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-audio-adapter", "module", NULL, NULL, NULL,
        (GAsyncReadyCallback) on_plugin_loaded, f);
    g_main_loop_run (f->base.loop);
  }
  {
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-audio-virtual", "module", NULL, NULL, NULL,
        (GAsyncReadyCallback) on_plugin_loaded, f);
    g_main_loop_run (f->base.loop);
  }
}

static void
test_si_audio_virtual_teardown (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
test_si_audio_virtual_configure_activate (TestFixture * f,
    gconstpointer user_data)
{
  g_autoptr (WpSessionItem) item = NULL;

  /* skip the test if null-audio-sink factory is not installed */
  if (!test_is_spa_lib_installed (&f->base, "support.null-audio-sink")) {
    g_test_skip ("The pipewire null-audio-sink factory was not found");
    return;
  }

  /* create item */

  item = wp_session_item_make (f->base.core, "si-audio-virtual");
  g_assert_nonnull (item);

  /* configure item */

  {
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_set (props, "name", "virtual");
    wp_properties_set (props, "media.class", "Audio/Source");
    g_assert_true (wp_session_item_configure (item, props));
    g_assert_true (wp_session_item_is_configured (item));
  }

  {
    const gchar *str = NULL;
    g_autoptr (WpProperties) props = wp_session_item_get_properties (item);
    g_assert_nonnull (props);
    str = wp_properties_get (props, "name");
    g_assert_nonnull (str);
    g_assert_cmpstr ("virtual", ==, str);
    str = wp_properties_get (props, "item.node.direction");
    g_assert_nonnull (str);
    g_assert_cmpstr ("output", ==, str);
    str = wp_properties_get (props, "item.factory.name");
    g_assert_nonnull (str);
    g_assert_cmpstr ("si-audio-virtual", ==, str);
  }

  /* activate item */

  wp_object_activate (WP_OBJECT (item), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL,  (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (item)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE);

  /* reset */
  wp_session_item_reset (item);
  g_assert_false (wp_session_item_is_configured (item));
}

static void
test_si_audio_virtual_export (TestFixture * f, gconstpointer user_data)
{
  g_autoptr (WpSessionItem) item = NULL;
  g_autoptr (WpObjectManager) clients_om = NULL;
  g_autoptr (WpClient) self_client = NULL;

  /* skip the test if null-audio-sink factory is not installed */
  if (!test_is_spa_lib_installed (&f->base, "support.null-audio-sink")) {
    g_test_skip ("The pipewire null-audio-sink factory was not found");
    return;
  }

  clients_om = wp_object_manager_new ();
  wp_object_manager_add_interest (clients_om, WP_TYPE_CLIENT, NULL);
  wp_object_manager_request_object_features (clients_om,
      WP_TYPE_CLIENT, WP_PROXY_FEATURE_BOUND);
  g_signal_connect_swapped (clients_om, "objects-changed",
      G_CALLBACK (g_main_loop_quit), f->base.loop);
  wp_core_install_object_manager (f->base.core, clients_om);
  g_main_loop_run (f->base.loop);
  self_client = wp_object_manager_lookup (clients_om, WP_TYPE_CLIENT, NULL);
  g_assert_nonnull (self_client);

  /* create item */

  item = wp_session_item_make (f->base.core, "si-audio-virtual");
  g_assert_nonnull (item);

  /* configure item */
  {
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_set (props, "name", "virtual");
    wp_properties_set (props, "media.class", "Audio/Source");
    g_assert_true (wp_session_item_configure (item, props));
    g_assert_true (wp_session_item_is_configured (item));
  }

  /* activate item */

  {
    wp_object_activate (WP_OBJECT (item),
        WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED,
        NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
    g_main_loop_run (f->base.loop);
    g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (item)), ==,
        WP_SESSION_ITEM_FEATURE_ACTIVE | WP_SESSION_ITEM_FEATURE_EXPORTED);
  }

  {
    g_autoptr (WpNode) n = NULL;
    g_autoptr (WpProperties) props = NULL;

    n = wp_session_item_get_associated_proxy (item, WP_TYPE_NODE);
    g_assert_nonnull (n);
    props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (n));
    g_assert_nonnull (props);

    g_assert_cmpstr (wp_properties_get (props, "media.class"), ==,
        "Audio/Source");
  }

  /* reset */
  wp_session_item_reset (item);
  g_assert_false (wp_session_item_is_configured (item));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  /* configure-activate */
  g_test_add ("/modules/si-audio-virtual/configure-activate",
      TestFixture, NULL,
      test_si_audio_virtual_setup,
      test_si_audio_virtual_configure_activate,
      test_si_audio_virtual_teardown);

 /* export */
 g_test_add ("/modules/si-audio-virtual/export",
      TestFixture, NULL,
      test_si_audio_virtual_setup,
      test_si_audio_virtual_export,
      test_si_audio_virtual_teardown);

  return g_test_run ();
}
