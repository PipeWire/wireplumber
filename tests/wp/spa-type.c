/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/test-log.h"
#include <spa/utils/type-info.h>

static void
test_spa_type_basic (void)
{
  g_assert_cmpuint (WP_SPA_TYPE_INVALID, ==, SPA_ID_INVALID);

  {
    WpSpaType type = SPA_TYPE_Int;
    g_assert_cmpstr (wp_spa_type_name (type), ==, "Spa:Int");
    g_assert_true (wp_spa_type_is_fundamental (type));
    g_assert_cmpuint (wp_spa_type_parent (type), ==, SPA_TYPE_Int);
  }

  {
    WpSpaType type = wp_spa_type_from_name ("Spa:Enum:ParamId");
    g_assert_cmpuint (type, ==, WP_SPA_TYPE_INVALID);

    WpSpaIdTable table = wp_spa_id_table_from_name ("Spa:Enum:ParamId");
    g_assert_nonnull (table);
  }

  {
    WpSpaType type = SPA_TYPE_OBJECT_Props;
    g_assert_cmpstr (wp_spa_type_name (type), ==, "Spa:Pod:Object:Param:Props");
    g_assert_cmpuint (wp_spa_type_from_name (SPA_TYPE_INFO_Props), ==, type);
    g_assert_true (wp_spa_type_is_object (type));
    g_assert_false (wp_spa_type_is_fundamental (type));
    g_assert_cmpuint (wp_spa_type_parent (type), ==, SPA_TYPE_Object);
    g_assert_nonnull (wp_spa_type_get_object_id_values_table (type));
    g_assert_true (wp_spa_type_get_object_id_values_table (type) ==
        wp_spa_id_table_from_name ("Spa:Enum:ParamId"));
  }

  /* enums */
  {
    WpSpaIdValue id = wp_spa_id_value_from_name ("Spa:Enum:ParamId:Props");
    g_assert_nonnull (id);
    g_assert_cmpstr (wp_spa_id_value_name (id), ==, "Spa:Enum:ParamId:Props");
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Props");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PARAM_Props);

    g_assert_true (id == wp_spa_id_value_from_short_name (
            "Spa:Enum:ParamId", "Props"));
    g_assert_true (id == wp_spa_id_value_from_number (
            "Spa:Enum:ParamId", SPA_PARAM_Props));
  }

  {
    WpSpaIdValue id =
        wp_spa_id_value_from_name ("Spa:Enum:Control:Properties");
    g_assert_nonnull (id);
    g_assert_cmpstr (wp_spa_id_value_name (id), ==,
        "Spa:Enum:Control:Properties");
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Properties");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_CONTROL_Properties);

    g_assert_true (id == wp_spa_id_value_from_short_name (
            "Spa:Enum:Control", "Properties"));
    g_assert_true (id == wp_spa_id_value_from_number (
            "Spa:Enum:Control", SPA_CONTROL_Properties));
  }

  {
    WpSpaIdValue id = wp_spa_id_value_from_name ("Spa:Enum:Choice:Enum");
    g_assert_nonnull (id);
    g_assert_cmpstr (wp_spa_id_value_name (id), ==, "Spa:Enum:Choice:Enum");
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Enum");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_CHOICE_Enum);

    g_assert_true (id == wp_spa_id_value_from_short_name (
            "Spa:Enum:Choice", "Enum"));
    g_assert_true (id == wp_spa_id_value_from_number (
            "Spa:Enum:Choice", SPA_CHOICE_Enum));
  }

  /* objects */
  {
    WpSpaIdValue id =
        wp_spa_id_value_from_name ("Spa:Pod:Object:Param:Props:mute");
    g_assert_nonnull (id);
    g_assert_cmpstr (wp_spa_id_value_name (id), ==,
        "Spa:Pod:Object:Param:Props:mute");
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "mute");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_mute);

    g_assert_true (id == wp_spa_id_value_from_short_name (
            SPA_TYPE_INFO_Props, "mute"));
    g_assert_true (id == wp_spa_id_value_from_number (
            SPA_TYPE_INFO_Props, SPA_PROP_mute));
  }

  {
    WpSpaIdValue id =
        wp_spa_id_value_from_name ("Spa:Pod:Object:Param:PropInfo:id");
    g_assert_nonnull (id);
    g_assert_cmpstr (wp_spa_id_value_name (id), ==,
        "Spa:Pod:Object:Param:PropInfo:id");
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "id");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_INFO_id);

    /* WpSpaIdValue is a pointer to static spa_type_info,
       so it should be the same on all queries */
    g_assert_true (id == wp_spa_id_value_from_short_name (
            SPA_TYPE_INFO_PropInfo, "id"));
    g_assert_true (id == wp_spa_id_value_from_number (
            SPA_TYPE_INFO_PropInfo, SPA_PROP_INFO_id));
  }

  /* array value type check */
  {
    WpSpaIdValue id =
        wp_spa_id_value_from_name ("Spa:Pod:Object:Param:Props:channelVolumes");
    g_assert_nonnull (id);
    g_assert_cmpstr (wp_spa_id_value_name (id), ==,
        "Spa:Pod:Object:Param:Props:channelVolumes");
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "channelVolumes");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_channelVolumes);

    g_assert_cmpuint (wp_spa_id_value_array_get_item_type (id, NULL), ==,
        SPA_TYPE_Float);
  }

  {
    WpSpaIdValue id =
        wp_spa_id_value_from_name ("Spa:Pod:Object:Param:Props:channelMap");
    g_assert_nonnull (id);
    g_assert_cmpstr (wp_spa_id_value_name (id), ==,
        "Spa:Pod:Object:Param:Props:channelMap");
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "channelMap");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_channelMap);

    WpSpaIdTable table = NULL;
    g_assert_cmpuint (wp_spa_id_value_array_get_item_type (id, &table), ==,
        SPA_TYPE_Id);
    g_assert_nonnull (table);
    g_assert_true (table == wp_spa_id_table_from_name ("Spa:Enum:AudioChannel"));
  }
}

static void
test_spa_type_iterate (void)
{
  {
    WpSpaType type = wp_spa_type_from_name (SPA_TYPE_INFO_PropInfo);
    g_assert_cmpuint (type, !=, WP_SPA_TYPE_INVALID);
    g_assert_true (wp_spa_type_is_object (type));

    WpSpaIdTable table = wp_spa_type_get_values_table (type);
    g_autoptr (WpIterator) it = wp_spa_id_table_new_iterator (table);
    g_auto (GValue) value = G_VALUE_INIT;
    WpSpaIdValue id = NULL;

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_INFO_START);
    table = NULL;
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, &table), ==, SPA_TYPE_Id);
    g_assert_nonnull (table);
    g_assert_true (table == wp_spa_id_table_from_name ("Spa:Enum:ParamId"));
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "id");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_INFO_id);
    table = NULL;
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, &table), ==, SPA_TYPE_Id);
    g_assert_nonnull (table);
    g_assert_true (table ==
        wp_spa_id_table_from_name ("Spa:Pod:Object:Param:Props"));
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "name");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_INFO_name);
    table = NULL;
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, &table), ==, SPA_TYPE_String);
    g_assert_null (table);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "type");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_INFO_type);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, &table), ==, SPA_TYPE_Pod);
    g_assert_null (table);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "labels");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_INFO_labels);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, &table), ==, SPA_TYPE_Struct);
    g_assert_null (table);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "container");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_PROP_INFO_container);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, &table), ==, SPA_TYPE_Id);
    g_assert_null (table);
    g_value_unset (&value);
  }

  {
    WpSpaIdTable table = wp_spa_id_table_from_name ("Spa:Enum:Choice");
    g_assert_nonnull (table);

    g_autoptr (WpIterator) it = wp_spa_id_table_new_iterator (table);
    g_auto (GValue) value = G_VALUE_INIT;
    WpSpaIdValue id = NULL;

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "None");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_CHOICE_None);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Int);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Range");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_CHOICE_Range);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Int);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Step");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_CHOICE_Step);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Int);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Enum");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_CHOICE_Enum);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Int);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Flags");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, SPA_CHOICE_Flags);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Int);
    g_value_unset (&value);
  }
}

static void
test_spa_type_register (void)
{
  static const struct spa_type_info custom_enum_info[] = {
    { 0, SPA_TYPE_Int, "Spa:Enum:CustomEnum:Invalid", NULL  },
    { 1, SPA_TYPE_Int, "Spa:Enum:CustomEnum:Valid", NULL  },
    { 0, 0, NULL, NULL }
  };

  static const struct spa_type_info custom_obj_info[] = {
    { 0, SPA_TYPE_Id, "Spa:Pod:Object:CustomObj:", custom_enum_info },
    { 1, SPA_TYPE_Int, "Spa:Pod:Object:CustomObj:id", NULL },
    { 2, SPA_TYPE_String, "Spa:Pod:Object:CustomObj:name", NULL },
    { 3, SPA_TYPE_Float, "Spa:Pod:Object:CustomObj:volume", NULL },
    { 4, SPA_TYPE_Rectangle, "Spa:Pod:Object:CustomObj:box", NULL },
    { 5, SPA_TYPE_Bytes, "Spa:Pod:Object:CustomObj:data", NULL },
    { 0, 0, NULL, NULL },
  };

  wp_spa_dynamic_type_init ();

  WpSpaIdTable enum_table =
      wp_spa_dynamic_id_table_register ("Spa:Enum:CustomEnum", custom_enum_info);
  WpSpaType obj_type = wp_spa_dynamic_type_register ("Spa:Pod:Object:CustomObj",
      SPA_TYPE_Object, custom_obj_info);

  g_assert_nonnull (enum_table);
  g_assert_true (obj_type != WP_SPA_TYPE_INVALID);

  g_assert_true (enum_table ==
      wp_spa_id_table_from_name ("Spa:Enum:CustomEnum"));

  {
    g_autoptr (WpIterator) it = wp_spa_id_table_new_iterator (enum_table);
    g_auto (GValue) value = G_VALUE_INIT;
    WpSpaIdValue id = NULL;

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Invalid");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, 0);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Int);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "Valid");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, 1);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Int);
    g_value_unset (&value);

    g_assert_false (wp_iterator_next (it, &value));
  }

  g_assert_cmpstr (wp_spa_type_name (obj_type), ==, "Spa:Pod:Object:CustomObj");
  g_assert_true (wp_spa_type_is_object (obj_type));
  g_assert_false (wp_spa_type_is_fundamental (obj_type));
  g_assert_cmpuint (wp_spa_type_parent (obj_type), ==, SPA_TYPE_Object);
  g_assert_cmpuint (obj_type, ==,
      wp_spa_type_from_name ("Spa:Pod:Object:CustomObj"));
  g_assert_true (enum_table ==
      wp_spa_type_get_object_id_values_table (obj_type));

  {
    WpSpaIdTable table = wp_spa_type_get_values_table (obj_type);
    g_autoptr (WpIterator) it = wp_spa_id_table_new_iterator (table);
    g_auto (GValue) value = G_VALUE_INIT;
    WpSpaIdValue id = NULL;

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, 0);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Id);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "id");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, 1);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Int);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "name");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, 2);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_String);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "volume");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, 3);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Float);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "box");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, 4);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Rectangle);
    g_value_unset (&value);

    g_assert_true (wp_iterator_next (it, &value));
    id = g_value_get_pointer (&value);
    g_assert_cmpstr (wp_spa_id_value_short_name (id), ==, "data");
    g_assert_cmpuint (wp_spa_id_value_number (id), ==, 5);
    g_assert_cmpuint (wp_spa_id_value_get_value_type (id, NULL), ==, SPA_TYPE_Bytes);
    g_value_unset (&value);

    g_assert_false (wp_iterator_next (it, &value));
  }

  wp_spa_dynamic_type_deinit ();
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/spa-type/basic", test_spa_type_basic);
  g_test_add_func ("/wp/spa-type/iterate", test_spa_type_iterate);
  g_test_add_func ("/wp/spa-type/register", test_spa_type_register);

  return g_test_run ();
}
