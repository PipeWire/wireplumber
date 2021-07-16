/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
} TestFixture;

static void
test_si_audio_adapter_setup (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&f->base, 0);

  /* load modules */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-spa-node-factory", NULL, NULL));
    g_assert_nonnull (pw_context_load_module (f->base.server.context,
            "libpipewire-module-adapter", NULL, NULL));
  }
  {
    g_autoptr (GError) error = NULL;
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-audio-adapter", "module", NULL, &error);
    g_assert_no_error (error);
  }
}

static void
test_si_audio_adapter_teardown (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
test_si_audio_adapter_configure_activate (TestFixture * f,
    gconstpointer user_data)
{
  g_autoptr (WpNode) node = NULL;
  g_autoptr (WpSessionItem) adapter = NULL;

  /* skip test if audiotestsrc is not installed */
  if (!test_is_spa_lib_installed (&f->base, "audiotestsrc")) {
    g_test_skip ("The pipewire audiotestsrc factory was not found");
    return;
  }

  /* create audiotestsrc adapter node */
  node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc.adapter",
          NULL));
  g_assert_nonnull (node);
  wp_object_activate (WP_OBJECT (node), WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* create adapter */
  adapter = wp_session_item_make (f->base.core, "si-audio-adapter");
  g_assert_nonnull (adapter);
  g_assert_true (WP_IS_SI_LINKABLE (adapter));

  /* configure */
  {
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_setf (props, "node", "%p", node);
    g_assert_true (wp_session_item_configure (adapter, props));
    g_assert_true (wp_session_item_is_configured (adapter));
  }

  /* validate configuration */
  {
    const gchar *str = NULL;
    g_autoptr (WpProperties) props = wp_session_item_get_properties (adapter);
    g_assert_nonnull (props);
    str = wp_properties_get (props, "name");
    g_assert_nonnull (str);
    g_assert_cmpstr ("audiotestsrc.adapter", ==, str);
    str = wp_properties_get (props, "media.class");
    g_assert_nonnull (str);
    g_assert_cmpstr ("Audio/Source", ==, str);
    str = wp_properties_get (props, "direction");
    g_assert_nonnull (str);
    g_assert_cmpstr ("1", ==, str);
    str = wp_properties_get (props, "enable.control.port");
    g_assert_nonnull (str);
    g_assert_cmpstr ("0", ==, str);
    str = wp_properties_get (props, "enable.monitor");
    g_assert_nonnull (str);
    g_assert_cmpstr ("0", ==, str);
    str = wp_properties_get (props, "is.device");
    g_assert_nonnull (str);
    g_assert_cmpstr ("1", ==, str);
    str = wp_properties_get (props, "si.factory.name");
    g_assert_nonnull (str);
    g_assert_cmpstr ("si-audio-adapter", ==, str);
  }

  /* activate */
  wp_object_activate (WP_OBJECT (adapter), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (adapter)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (node)), ==,
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL | WP_NODE_FEATURE_PORTS);

  /* deactivate - configuration should not be altered  */
  wp_object_deactivate (WP_OBJECT (adapter), WP_SESSION_ITEM_FEATURE_ACTIVE);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (adapter)), ==, 0);
  g_assert_true (wp_session_item_is_configured (adapter));

  /* reset */
  wp_session_item_reset (adapter);
  g_assert_false (wp_session_item_is_configured (adapter));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  /* configure-activate */

  g_test_add ("/modules/si-audio-adapter/configure-activate",
      TestFixture, NULL,
      test_si_audio_adapter_setup,
      test_si_audio_adapter_configure_activate,
      test_si_audio_adapter_teardown);

  return g_test_run ();
}
