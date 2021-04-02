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
test_si_audio_convert_setup (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&f->base, 0);

  /* load modules */
  {
    g_autoptr (WpTestServerLocker) lock =
        wp_test_server_locker_new (&f->base.server);

    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "fake*", "test/libspa-test"), ==, 0);
    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
    g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
            "audio.convert", "audioconvert/libspa-audioconvert"), ==, 0);
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
  {
    g_autoptr (GError) error = NULL;
    wp_core_load_component (f->base.core,
        "libwireplumber-module-si-audio-convert", "module", NULL, &error);
    g_assert_no_error (error);
  }
}

static void
test_si_audio_convert_teardown (TestFixture * f, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&f->base);
}

static void
test_si_audio_convert_configure_activate (TestFixture * f,
    gconstpointer user_data)
{
  g_autoptr (WpNode) target_node = NULL;
  g_autoptr (WpSessionItem) target = NULL;
  g_autoptr (WpSessionItem) convert = NULL;

  /* create target node */

  target_node = wp_node_new_from_factory (f->base.core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", "audiotestsrc.adapter",
          NULL));
  g_assert_nonnull (target_node);
  wp_object_activate (WP_OBJECT (target_node),
      WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL, NULL,
      (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);

  /* create target */

  target = wp_session_item_make (f->base.core, "si-audio-adapter");
  g_assert_nonnull (target);
  g_assert_true (WP_IS_SI_PORT_INFO (target));

  /* configure target */

  {
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_setf (props, "node", "%p", target_node);
    g_assert_true (wp_session_item_configure (target, props));
    g_assert_true (wp_session_item_is_configured (target));
  }

  /* create convert */

  convert = wp_session_item_make (f->base.core, "si-audio-convert");
  g_assert_nonnull (convert);
  g_assert_true (WP_IS_SI_PORT_INFO (convert));

  /* configure convert */

  {
    WpProperties *props = wp_properties_new_empty ();
    wp_properties_setf (props, "target", "%p", target);
    wp_properties_set (props, "name", "convert");
    g_assert_true (wp_session_item_configure (convert, props));
    g_assert_true (wp_session_item_is_configured (convert));
  }

  {
    const gchar *str = NULL;
    g_autoptr (WpProperties) props = wp_session_item_get_properties (convert);
    g_assert_nonnull (props);
    str = wp_properties_get (props, "name");
    g_assert_nonnull (str);
    g_assert_cmpstr ("convert", ==, str);
    str = wp_properties_get (props, "direction");
    g_assert_nonnull (str);
    g_assert_cmpstr ("1", ==, str);
    str = wp_properties_get (props, "enable.control.port");
    g_assert_nonnull (str);
    g_assert_cmpstr ("0", ==, str);
    str = wp_properties_get (props, "si.factory.name");
    g_assert_nonnull (str);
    g_assert_cmpstr ("si-audio-convert", ==, str);
  }

  /* activate convert */

  wp_object_activate (WP_OBJECT (convert), WP_SESSION_ITEM_FEATURE_ACTIVE,
      NULL,  (GAsyncReadyCallback) test_object_activate_finish_cb, f);
  g_main_loop_run (f->base.loop);
  g_assert_cmphex (wp_object_get_active_features (WP_OBJECT (convert)), ==,
      WP_SESSION_ITEM_FEATURE_ACTIVE);

  /* reset */
  wp_session_item_reset (convert);
  g_assert_false (wp_session_item_is_configured (convert));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  /* configure-activate */
  g_test_add ("/modules/si-audio-convert/configure-activate",
      TestFixture, NULL,
      test_si_audio_convert_setup,
      test_si_audio_convert_configure_activate,
      test_si_audio_convert_teardown);

  return g_test_run ();
}
