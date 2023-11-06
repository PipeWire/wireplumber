/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/test-log.h"

static void
test_match_rules_update_properties (void)
{
  static const gchar * const rules_json_string =
      "["
      "  {"
      "    matches = ["
      "      {"
      "        device.name = \"~alsa_card.*\""
      "      }"
      "    ]"
      "    actions = {"
      "      update-props = {"
      "        api.alsa.use-acp = true"
      "        api.acp.auto-port = false"
      "      }"
      "    }"
      "  }"
      "  {"
      "    matches = ["
      "      {"
      "        node.name = \"alsa_output.0.my-alsa-device\""
      "      }"
      "    ]"
      "    actions = {"
      "      update-props = {"
      "        audio.rate = 96000"
      "        node.description = \"My ALSA Node\""
      "        media.class = null"
      "      }"
      "    }"
      "  }"
      "]";

  g_autoptr (WpSpaJson) rules = wp_spa_json_new_from_stringn (rules_json_string,
      strlen (rules_json_string));
  g_assert_nonnull (rules);

  /* Unmatched */
  {
    g_autoptr (WpProperties) match_props = NULL;

    match_props = wp_properties_new (
        "device.name", "unmatched-device-name",
        NULL);

    g_assert_cmpint (wp_json_utils_match_rules_update_properties (rules, match_props), ==, 0);
  }

  /* Match regex with props filled */
  {
    g_autoptr (WpProperties) match_props = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card_0.my-alsa-device",
        NULL);
    g_assert_nonnull (match_props);

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card_0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_null (str);
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);

    g_assert_cmpint (wp_json_utils_match_rules_update_properties (rules, match_props), ==, 2);

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card_0.my-alsa-device");
    str = wp_properties_get (match_props, "api.alsa.use-acp");
    g_assert_cmpstr (str, ==, "true");
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_cmpstr (str, ==, "false");
  }

  /* Match equal with props filled */
  {
    g_autoptr (WpProperties) match_props = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "node.name", "alsa_output.0.my-alsa-device",
        NULL);
    g_assert_nonnull (match_props);

    str = wp_properties_get (match_props, "node.name");
    g_assert_cmpstr (str, ==, "alsa_output.0.my-alsa-device");
    str = wp_properties_get (match_props, "audio.rate");
    g_assert_null (str);
    str = wp_properties_get (match_props, "node.description");
    g_assert_null (str);
    str = wp_properties_get (match_props, "media.class");
    g_assert_null (str);

    g_assert_cmpint (wp_json_utils_match_rules_update_properties (rules, match_props), ==, 2);

    str = wp_properties_get (match_props, "node.name");
    g_assert_cmpstr (str, ==, "alsa_output.0.my-alsa-device");
    str = wp_properties_get (match_props, "audio.rate");
    g_assert_cmpstr (str, ==, "96000");
    str = wp_properties_get (match_props, "node.description");
    g_assert_cmpstr (str, ==, "My ALSA Node");
    str = wp_properties_get (match_props, "media.class");
    g_assert_null (str);
  }

  /* Match equal with 1 prop updated */
  {
    g_autoptr (WpProperties) match_props = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "node.name", "alsa_output.0.my-alsa-device",
        "audio.rate", "96000",
        "node.description", "Test",
        NULL);
    g_assert_nonnull (match_props);

    str = wp_properties_get (match_props, "node.name");
    g_assert_cmpstr (str, ==, "alsa_output.0.my-alsa-device");
    str = wp_properties_get (match_props, "audio.rate");
    g_assert_cmpstr (str, ==, "96000");
    str = wp_properties_get (match_props, "node.description");
    g_assert_cmpstr (str, ==, "Test");
    str = wp_properties_get (match_props, "media.class");
    g_assert_null (str);

    g_assert_cmpint (wp_json_utils_match_rules_update_properties (rules, match_props), ==, 1);

    str = wp_properties_get (match_props, "node.name");
    g_assert_cmpstr (str, ==, "alsa_output.0.my-alsa-device");
    str = wp_properties_get (match_props, "audio.rate");
    g_assert_cmpstr (str, ==, "96000");
    str = wp_properties_get (match_props, "node.description");
    g_assert_cmpstr (str, ==, "My ALSA Node");
    str = wp_properties_get (match_props, "media.class");
    g_assert_null (str);
  }

  /* Match equal with prop deleted */
  {
    g_autoptr (WpProperties) match_props = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "node.name", "alsa_output.0.my-alsa-device",
        "media.class", "Audio/Sink",
        "audio.rate", "48000",
        "node.description", "Test",
        NULL);
    g_assert_nonnull (match_props);

    str = wp_properties_get (match_props, "node.name");
    g_assert_cmpstr (str, ==, "alsa_output.0.my-alsa-device");
    str = wp_properties_get (match_props, "audio.rate");
    g_assert_cmpstr (str, ==, "48000");
    str = wp_properties_get (match_props, "node.description");
    g_assert_cmpstr (str, ==, "Test");
    str = wp_properties_get (match_props, "media.class");
    g_assert_cmpstr (str, ==, "Audio/Sink");

    g_assert_cmpint (wp_json_utils_match_rules_update_properties (rules, match_props), ==, 3);

    str = wp_properties_get (match_props, "node.name");
    g_assert_cmpstr (str, ==, "alsa_output.0.my-alsa-device");
    str = wp_properties_get (match_props, "audio.rate");
    g_assert_cmpstr (str, ==, "96000");
    str = wp_properties_get (match_props, "node.description");
    g_assert_cmpstr (str, ==, "My ALSA Node");
    str = wp_properties_get (match_props, "media.class");
    g_assert_null (str);
  }
}

static gboolean
match_rules_cb (gpointer data, const gchar * action, WpSpaJson * value,
    GError ** error)
{
  WpProperties *match_props = data;

  if (g_str_equal (action, "update-props")) {
    wp_properties_update_from_json (match_props, value);
  }
  else if (g_str_equal (action, "set-answer")) {
    g_autofree gchar *str = wp_spa_json_to_string (value);
    wp_properties_set (match_props, "answer.universe", str);
  }
  else if (g_str_equal (action, "generate-error")) {
    g_autofree gchar *str = wp_spa_json_parse_string (value);
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "error: %s", str);
    return FALSE;
  }
  else if (g_str_equal (action, "set-description")) {
    g_autofree gchar *str = wp_spa_json_parse_string (value);
    wp_properties_set (match_props, "device.description", str);
  }

  return TRUE;
}

static void
test_match_rules (void)
{
  static const gchar * const rules_json_string =
      "["
      "  {"
      "    matches = ["
      "      {"
      "        device.name = \"~alsa_card.*\""
      "      }"
      "    ]"
      "    actions = {"
      "      update-props = {"
      "        device.name = alsa_card.1"
      "        api.acp.auto-port = false"
      "      }"
      "      set-answer = 42"
      "    }"
      "  }"
      "  {"
      "    matches = ["
      "      {"
      "        test.error = true"
      "      }"
      "    ]"
      "    actions = {"
      "      generate-error = \"test.error is true\""
      "    }"
      "  }"
      "  {"
      "    matches = ["
      "      {"
      "        device.name = \"alsa_card.1\""
      "      }"
      "    ]"
      "    actions = {"
      "      set-description = \"My ALSA Device\""
      "    }"
      "  }"
      "]";

  g_autoptr (WpSpaJson) rules = wp_spa_json_new_from_stringn (rules_json_string,
      strlen (rules_json_string));
  g_assert_nonnull (rules);

  /* no error */
  {
    g_autoptr (GError) error = NULL;
    g_autoptr (WpProperties) match_props = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card.0",
        "test.error", "false",
        NULL);

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.0");
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "answer.universe");
    g_assert_null (str);
    str = wp_properties_get (match_props, "test.error");
    g_assert_cmpstr (str, ==, "false");
    str = wp_properties_get (match_props, "device.description");
    g_assert_null (str);

    g_assert_true (wp_json_utils_match_rules (rules, match_props, match_rules_cb,
        match_props, &error));
    g_assert_no_error (error);

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.1");
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_cmpstr (str, ==, "false");
    str = wp_properties_get (match_props, "answer.universe");
    g_assert_cmpstr (str, ==, "42");
    str = wp_properties_get (match_props, "test.error");
    g_assert_cmpstr (str, ==, "false");
    str = wp_properties_get (match_props, "device.description");
    g_assert_cmpstr (str, ==, "My ALSA Device");
  }

  /* with error */
  {
    g_autoptr (GError) error = NULL;
    g_autoptr (WpProperties) match_props = NULL;
    const gchar *str = NULL;

    match_props = wp_properties_new (
        "device.name", "alsa_card.256",
        "test.error", "true",
        NULL);

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.256");
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_null (str);
    str = wp_properties_get (match_props, "answer.universe");
    g_assert_null (str);
    str = wp_properties_get (match_props, "test.error");
    g_assert_cmpstr (str, ==, "true");
    str = wp_properties_get (match_props, "device.description");
    g_assert_null (str);

    g_assert_false (wp_json_utils_match_rules (rules, match_props, match_rules_cb,
        match_props, &error));
    g_assert_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED);
    g_assert_cmpstr (error->message, ==, "error: test.error is true");

    str = wp_properties_get (match_props, "device.name");
    g_assert_cmpstr (str, ==, "alsa_card.1");
    str = wp_properties_get (match_props, "api.acp.auto-port");
    g_assert_cmpstr (str, ==, "false");
    str = wp_properties_get (match_props, "answer.universe");
    g_assert_cmpstr (str, ==, "42");
    str = wp_properties_get (match_props, "test.error");
    g_assert_cmpstr (str, ==, "true");
    str = wp_properties_get (match_props, "device.description");
    g_assert_null (str);
  }
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/json-utils/match_rules_update_props",
      test_match_rules_update_properties);
  g_test_add_func ("/wp/json-utils/match_rules", test_match_rules);

  return g_test_run ();
}
