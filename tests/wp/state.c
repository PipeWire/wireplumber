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
    g_assert_true (wp_state_save (state, "group", props));
  }

  /* Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "group");
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
    g_assert_true (wp_state_save (state, "group", props));
  }

  /* Re-Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "group");
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "new-key"), ==, "new-value");
    g_assert_null (wp_properties_get (props, "key1"));
    g_assert_null (wp_properties_get (props, "key2"));
    g_assert_null (wp_properties_get (props, "key3"));
  }

  wp_state_clear (state);

  /* Load empty */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "group");
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
  g_autoptr (WpState) state = wp_state_new ("empty");
  g_assert_nonnull (state);

  /* Save */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "key", "value");
    g_assert_true (wp_state_save (state, "group", props));
  }

  /* Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "group");
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "key"), ==, "value");
  }

  /* Save empty */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    g_assert_true (wp_state_save (state, "group", props));
  }

  /* Load empty */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "group");
    g_assert_nonnull (props);
    g_assert_null (wp_properties_get (props, "key"));
  }

  wp_state_clear (state);
}

static void
test_state_spaces (void)
{
  g_autoptr (WpState) state = wp_state_new ("spaces");
  g_assert_nonnull (state);

  /* Save */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "key", "value with spaces");
    g_assert_true (wp_state_save (state, "group", props));
  }

  /* Load */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "group");
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "key"), ==, "value with spaces");
  }

  wp_state_clear (state);
}

static void
test_state_group (void)
{
  g_autoptr (WpState) state = wp_state_new ("group");
  g_assert_nonnull (state);

  /* Save 1 */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "key1", "value1");
    g_assert_true (wp_state_save (state, "1", props));
  }

  /* Save 2 */
  {
    g_autoptr (WpProperties) props = wp_properties_new_empty ();
    wp_properties_set (props, "key2", "value2");
    g_assert_true (wp_state_save (state, "2", props));
  }

  /* Load invalid group */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "invalid");
    g_assert_nonnull (props);
    g_assert_null (wp_properties_get (props, "key1"));
    g_assert_null (wp_properties_get (props, "key2"));
  }

  /* Load 1 */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "1");
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "key1"), ==, "value1");
    g_assert_null (wp_properties_get (props, "key2"));
  }

  /* Load 2 */
  {
    g_autoptr (WpProperties) props = wp_state_load (state, "2");
    g_assert_nonnull (props);
    g_assert_cmpstr (wp_properties_get (props, "key2"), ==, "value2");
    g_assert_null (wp_properties_get (props, "key1"));
  }

  wp_state_clear (state);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/state/basic", test_state_basic);
  g_test_add_func ("/wp/state/empty", test_state_empty);
  g_test_add_func ("/wp/state/spaces", test_state_spaces);
  g_test_add_func ("/wp/state/group", test_state_group);

  return g_test_run ();
}
