/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <spa/utils/type-info.h>

static void
test_spa_type_basic (void)
{
  wp_spa_type_init (TRUE);

  /* Make sure table sizes are not 0 */
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_BASIC), >, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_PARAM), >, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_PROPS), >, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_PROP_INFO), >, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_CONTROL), >, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_CHOICE), >, 0);

  /* Make sure SPA_TYPE_OBJECT_Props type from WP_SPA_TYPE_TABLE_BASIC is registered */
  {
    const char *name = NULL;
    const char *nick = NULL;
    WpSpaTypeTable table = WP_SPA_TYPE_TABLE_BASIC;
    g_assert_true (wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_BASIC, SPA_TYPE_OBJECT_Props,
        &name, &nick, &table));
    g_assert_cmpstr (name, ==, "Spa:Pod:Object:Param:Props");
    g_assert_cmpstr (nick, ==, "Props");
    g_assert_cmpuint (table, ==, WP_SPA_TYPE_TABLE_PROPS);
  }

  /* Make sure SPA_PARAM_Props type from WP_SPA_TYPE_TABLE_PARAM is registered */
  {
    const char *name = NULL;
    const char *nick = NULL;
    g_assert_true (wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_PARAM, SPA_PARAM_Props, &name, &nick, NULL));
    g_assert_cmpstr (name, ==, "Spa:Enum:ParamId:Props");
    g_assert_cmpstr (nick, ==, "Props");
  }

  /* Make sure SPA_PROP_mute type from WP_SPA_TYPE_TABLE_PROPS is registered */
  {
    const char *name = NULL;
    const char *nick = NULL;
    g_assert_true (wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_PROPS, SPA_PROP_mute, &name, &nick, NULL));
    g_assert_cmpstr (name, ==, "Spa:Pod:Object:Param:Props:mute");
    g_assert_cmpstr (nick, ==, "mute");
  }

  /* Make sure SPA_PROP_INFO_id type from WP_SPA_TYPE_TABLE_PROP_INFO is registered */
  {
    const char *name = NULL;
    const char *nick = NULL;
    g_assert_true (wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_PROP_INFO, SPA_PROP_INFO_id, &name, &nick, NULL));
    g_assert_cmpstr (name, ==, "Spa:Pod:Object:Param:PropInfo:id");
    g_assert_cmpstr (nick, ==, "id");
  }

  /* Make sure SPA_CONTROL_Properties type from WP_SPA_TYPE_TABLE_CONTROL is registered */
  {
    const char *name = NULL;
    const char *nick = NULL;
    g_assert_true (wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_CONTROL, SPA_CONTROL_Properties, &name, &nick, NULL));
    g_assert_cmpstr (name, ==, "Spa:Enum:Control:Properties");
    g_assert_cmpstr (nick, ==, "Properties");
  }

  /* Make sure SPA_CHOICE_Enum type from WP_SPA_TYPE_TABLE_CHOICE is registered */
  {
    const char *name = NULL;
    const char *nick = NULL;
    g_assert_true (wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_CHOICE, SPA_CHOICE_Enum, &name, &nick, NULL));
    g_assert_cmpstr (name, ==, "Spa:Enum:Choice:Enum");
    g_assert_cmpstr (nick, ==, "Enum");
  }

  wp_spa_type_deinit ();
}

static void
test_spa_type_register (void)
{
  wp_spa_type_init (FALSE);

  /* Make sure no types are registered */
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_BASIC), ==, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_PARAM), ==, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_PROPS), ==, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_CONTROL), ==, 0);
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_CHOICE), ==, 0);

  /* Register SPA_TYPE_Bool */
  {
    g_assert_true (wp_spa_type_register (WP_SPA_TYPE_TABLE_BASIC, "Spa:Bool", "spa-bool"));

    guint32 id = 0;
    const char *name = NULL;
    g_assert_true (wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, "spa-bool", &id, &name, NULL));
    g_assert_cmpuint (id, ==, SPA_TYPE_Bool);
    g_assert_cmpstr (name, ==, "Spa:Bool");

    name = NULL;
    const char *nick = NULL;
    g_assert_true (wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_BASIC, id, &name, &nick, NULL));
    g_assert_cmpstr (name, ==, "Spa:Bool");
    g_assert_cmpstr (nick, ==, "spa-bool");

    g_assert_false (wp_spa_type_register (WP_SPA_TYPE_TABLE_BASIC, "Spa:Bool", "spa-bool"));

    g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_BASIC), ==, 1);
  }

  /* Register Custom */
  {
    g_assert_true (wp_spa_type_register (WP_SPA_TYPE_TABLE_BASIC, "Wp:Bool", "wp-bool"));

    guint32 id = 0;
    const char *name = NULL;
    g_assert_true (wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, "wp-bool", &id, &name, NULL));
    g_assert_cmpuint (id, ==, SPA_TYPE_VENDOR_Other + 1);
    g_assert_cmpstr (name, ==, "Wp:Bool");

    name = NULL;
    const char *nick = NULL;
    g_assert_true (wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_BASIC, id, &name, &nick, NULL));
    g_assert_cmpstr (name, ==, "Wp:Bool");
    g_assert_cmpstr (nick, ==, "wp-bool");

    g_assert_false (wp_spa_type_register (WP_SPA_TYPE_TABLE_BASIC, "Wp:Bool", "wp-bool"));

    g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_BASIC), ==, 2);
  }

  /* Unregister SPA_TYPE_Bool */
  g_assert_true (wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, "spa-bool", NULL, NULL, NULL));
  wp_spa_type_unregister (WP_SPA_TYPE_TABLE_BASIC, "spa-bool");
  g_assert_false (wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, "spa-bool", NULL, NULL, NULL));
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_BASIC), ==, 1);

  /* Unregister Custom */
  g_assert_true (wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, "wp-bool", NULL, NULL, NULL));
  wp_spa_type_unregister (WP_SPA_TYPE_TABLE_BASIC, "wp-bool");
  g_assert_false (wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_BASIC, "wp-bool", NULL, NULL, NULL));
  g_assert_cmpuint (wp_spa_type_get_table_size (WP_SPA_TYPE_TABLE_BASIC), ==, 0);

  wp_spa_type_deinit ();
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/spa-type/basic", test_spa_type_basic);
  g_test_add_func ("/wp/spa-type/register", test_spa_type_register);

  return g_test_run ();
}
