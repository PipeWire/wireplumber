/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

static void
test_state_basic (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpState) state = wp_state_new ("basic");
  g_assert_nonnull (state);

  g_assert_cmpstr (wp_state_get_name (state), ==, "basic");
  g_assert_true (g_str_has_suffix (wp_state_get_location (state), "basic"));

  /* Save */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "key1", "value1");
    wp_properties_set (props, "key2", "value2");
    wp_properties_set (props, "key3", "value3");
    g_assert_true (wp_state_save (state, props, &error));
    g_assert_no_error (error);
  }

  /* Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state);
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "key1"), ==, "value1");
    g_assert_cmpstr (wp_properties_get (props, "key2"), ==, "value2");
    g_assert_cmpstr (wp_properties_get (props, "key2"), ==, "value2");
    g_assert_null (wp_properties_get (props, "invalid"));
  }

  /* Re-Save */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "new-key", "new-value");
    g_assert_true (wp_state_save (state, props, &error));
    g_assert_no_error (error);
  }

  /* Re-Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state);
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "new-key"), ==, "new-value");
    g_assert_null (wp_properties_get (props, "key1"));
    g_assert_null (wp_properties_get (props, "key2"));
    g_assert_null (wp_properties_get (props, "key3"));
  }

  wp_state_clear (state);

  /* Load empty */
  {
    g_autoptr (WpProperties) props = wp_state_load (state);
    g_assert_nonnull (props);
    g_assert_null (wp_properties_get (props, "new-key"));
    g_assert_null (wp_properties_get (props, "key1"));
    g_assert_null (wp_properties_get (props, "key2"));
    g_assert_null (wp_properties_get (props, "key3"));
  }

  wp_state_clear (state);
}

static void
test_state_empty (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpState) state = wp_state_new ("empty");
  g_assert_nonnull (state);

  /* Save */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "key", "value");
    g_assert_true (wp_state_save (state, props, &error));
    g_assert_no_error (error);
  }

  /* Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state);
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "key"), ==, "value");
  }

  /* Save empty */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    g_assert_true (wp_state_save (state, props, &error));
    g_assert_no_error (error);
  }

  /* Load empty */
  {
    g_autoptr (WpProperties) props = wp_state_load (state);
    g_assert_nonnull (props);
    g_assert_null (wp_properties_get (props, "key"));
  }

  wp_state_clear (state);
}

static void
test_state_spaces (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpState) state = wp_state_new ("spaces");
  g_assert_nonnull (state);

  /* Save */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "key", "value with spaces");
    g_assert_true (wp_state_save (state, props, &error));
    g_assert_no_error (error);
  }

  /* Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state);
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "key"), ==, "value with spaces");
  }

  wp_state_clear (state);
}

static void
test_state_escaped (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (WpState) state = wp_state_new ("escaped");
  g_assert_nonnull (state);

  /* Save */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "[]", "v0");
    wp_properties_set (props, "[ ]", "v1");
    wp_properties_set (props, "[=]", "v2");
    wp_properties_set (props, " [=]", "v3");
    wp_properties_set (props, "[=] ", "v4");
    wp_properties_set (props, " [=] ", "v5");
    wp_properties_set (props, " [ =] ", "v6");
    wp_properties_set (props, " [= ] ", "v7");
    wp_properties_set (props, " [ = ] ", "v8");
    wp_properties_set (props, " [", "v9");
    wp_properties_set (props, "[ ", "v10");
    wp_properties_set (props, " [ ", "v11");
    wp_properties_set (props, " ]", "v12");
    wp_properties_set (props, "] ", "v13");
    wp_properties_set (props, " ] ", "v14");
    wp_properties_set (props, " ", "v15");
    wp_properties_set (props, "=", "v16");
    wp_properties_set (props, "\\", "v17");
    wp_properties_set (props, "\\[", "v18");
    wp_properties_set (props, "\\a", "v19");
    wp_properties_set (props, "\\\\", "v20");
    wp_properties_set (props, "[][", "][]");
    g_assert_true (wp_state_save (state, props, &error));
    g_assert_no_error (error);
  }

  /* Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state);
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "[]"), ==, "v0");
    g_assert_cmpstr (wp_properties_get (props, "[ ]"), ==, "v1");
    g_assert_cmpstr (wp_properties_get (props, "[=]"), ==, "v2");
    g_assert_cmpstr (wp_properties_get (props, " [=]"), ==, "v3");
    g_assert_cmpstr (wp_properties_get (props, "[=] "), ==, "v4");
    g_assert_cmpstr (wp_properties_get (props, " [=] "), ==, "v5");
    g_assert_cmpstr (wp_properties_get (props, " [ =] "), ==, "v6");
    g_assert_cmpstr (wp_properties_get (props, " [= ] "), ==, "v7");
    g_assert_cmpstr (wp_properties_get (props, " [ = ] "), ==, "v8");
    g_assert_cmpstr (wp_properties_get (props, " ["), ==, "v9");
    g_assert_cmpstr (wp_properties_get (props, "[ "), ==, "v10");
    g_assert_cmpstr (wp_properties_get (props, " [ "), ==, "v11");
    g_assert_cmpstr (wp_properties_get (props, " ]"), ==, "v12");
    g_assert_cmpstr (wp_properties_get (props, "] "), ==, "v13");
    g_assert_cmpstr (wp_properties_get (props, " ] "), ==, "v14");
    g_assert_cmpstr (wp_properties_get (props, " "), ==, "v15");
    g_assert_cmpstr (wp_properties_get (props, "="), ==, "v16");
    g_assert_cmpstr (wp_properties_get (props, "\\"), ==, "v17");
    g_assert_cmpstr (wp_properties_get (props, "\\["), ==, "v18");
    g_assert_cmpstr (wp_properties_get (props, "\\a"), ==, "v19");
    g_assert_cmpstr (wp_properties_get (props, "\\\\"), ==, "v20");
    g_assert_cmpstr (wp_properties_get (props, "[]["), ==, "][]");
  }
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/state/basic", test_state_basic);
  g_test_add_func ("/wp/state/empty", test_state_empty);
  g_test_add_func ("/wp/state/spaces", test_state_spaces);
  g_test_add_func ("/wp/state/escaped", test_state_escaped);

  return g_test_run ();
}
