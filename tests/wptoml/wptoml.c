/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wptoml/wptoml.h>

#define TOML_FILE_BASIC_TABLE "files/basic-table.toml"
#define TOML_FILE_BASIC_ARRAY "files/basic-array.toml"
#define TOML_FILE_NESTED_ARRAY "files/nested-array.toml"
#define TOML_FILE_NESTED_TABLE "files/nested-table.toml"
#define TOML_FILE_TABLE_ARRAY "files/table-array.toml"

static void
test_basic_table (void)
{
  /* Parse the file */
  const char *file_name = TOML_FILE_BASIC_TABLE;
  g_autoptr (WpTomlFile) file = wp_toml_file_new (file_name);
  g_assert_nonnull (file);

  /* Get the name */
  const char *name = wp_toml_file_get_name (file);
  g_assert_cmpstr (name, ==, TOML_FILE_BASIC_TABLE);

  /* Get the table */
  g_autoptr (WpTomlTable) table = wp_toml_file_get_table (file);
  g_assert_nonnull (table);

  /* Test contains */
  g_assert_false (wp_toml_table_contains (table, "invalid-key"));
  g_assert_true (wp_toml_table_contains (table, "bool"));

  /* Test boolean */
  {
    gboolean val = FALSE;
    g_assert_false (wp_toml_table_get_boolean (table, "invalid-key", &val));
    g_assert_false (val);
    g_assert_true (wp_toml_table_get_boolean (table, "bool", &val));
    g_assert_true (val);
  }

  /* Test int8 */
  {
    int8_t val = 0;
    g_assert_false (wp_toml_table_get_int8 (table, "invalid-key", &val));
    g_assert_cmpint (val, ==, 0);
    g_assert_true (wp_toml_table_get_int8 (table, "int8", &val));
    g_assert_cmpint (val, ==, -8);
  }

  /* Test uint8 */
  {
    uint8_t val = 0;
    g_assert_false (wp_toml_table_get_uint8 (table, "invalid-key", &val));
    g_assert_cmpuint (val, ==, 0);
    g_assert_true (wp_toml_table_get_uint8 (table, "uint8", &val));
    g_assert_cmpuint (val, ==, 8);
  }

  /* Test int16 */
  {
    int16_t val = 0;
    g_assert_false (wp_toml_table_get_int16 (table, "invalid-key", &val));
    g_assert_cmpint (val, ==, 0);
    g_assert_true (wp_toml_table_get_int16 (table, "int16", &val));
    g_assert_cmpint (val, ==, -16);
  }

  /* Test uint16 */
  {
    uint16_t val = 0;
    g_assert_false (wp_toml_table_get_uint16 (table, "invalid-key", &val));
    g_assert_cmpuint (val, ==, 0);
    g_assert_true (wp_toml_table_get_uint16 (table, "uint16", &val));
    g_assert_cmpuint (val, ==, 16);
  }

  /* Test int32 */
  {
    int32_t val = 0;
    g_assert_false (wp_toml_table_get_int32 (table, "invalid-key", &val));
    g_assert_cmpint (val, ==, 0);
    g_assert_true (wp_toml_table_get_int32 (table, "int32", &val));
    g_assert_cmpint (val, ==, -32);
  }

  /* Test uint32 */
  {
    uint32_t val = 0;
    g_assert_false (wp_toml_table_get_uint32 (table, "invalid-key", &val));
    g_assert_cmpuint (val, ==, 0);
    g_assert_true (wp_toml_table_get_uint32 (table, "uint32", &val));
    g_assert_cmpuint (val, ==, 32);
  }

  /* Test int64 */
  {
    int64_t val = 0;
    g_assert_false (wp_toml_table_get_int64 (table, "invalid-key", &val));
    g_assert_cmpint (val, ==, 0);
    g_assert_true (wp_toml_table_get_int64 (table, "int64", &val));
    g_assert_cmpint (val, ==, -64);
  }

  /* Test uint64 */
  {
    uint64_t val = 0;
    g_assert_false (wp_toml_table_get_uint64 (table, "invalid-key", &val));
    g_assert_true (val == 0);
    g_assert_true (wp_toml_table_get_uint64 (table, "uint64", &val));
    g_assert_true (val == 64);
  }

  /* Test double */
  {
    double val = 0.0;
    g_assert_false (wp_toml_table_get_double (table, "invalid-key", &val));
    g_assert_cmpfloat_with_epsilon (val, 0.0, 0.01);
    g_assert_true (wp_toml_table_get_double (table, "double", &val));
    g_assert_cmpfloat_with_epsilon (val, 3.14, 0.01);
  }

  /* Test string */
  {
    g_autofree char *val = wp_toml_table_get_string (table, "invalid-key");
    g_assert_null (val);
    val = wp_toml_table_get_string (table, "str");
    g_assert_nonnull (val);
    g_assert_cmpstr (val, ==, "str");
  }

  /* Test big string */
  {
    g_autofree char *val = wp_toml_table_get_string (table, "invalid-key");
    g_assert_null (val);
    val = wp_toml_table_get_string (table, "big_str");
    g_assert_nonnull (val);
    g_assert_cmpstr(val, ==, "this is a big string with special "
        "characters (!@#$%^&&*'') to make sure the wptoml library parses "
        "it correctly");
  }
}

static void
boolean_array_for_each (const gboolean *v, gpointer user_data)
{
  int64_t *total_trues = user_data;
  g_assert_nonnull (total_trues);

  /* Test all the array values could be parsed into boolean correctly */
  g_assert_nonnull (v);

  /* Count the trues */
  if (*v)
    (*total_trues)++;
}

static void
int64_array_for_each (const int64_t *v, gpointer user_data)
{
  int64_t *total = user_data;
  g_assert_nonnull (total);

  /* Test all the array values could be parsed into int64_t correctly */
  g_assert_nonnull (v);

  /* Add the value to the total */
  *total += *v;
}

static void
double_array_for_each (const double *v, gpointer user_data)
{
  double *total = user_data;
  g_assert_nonnull (total);

  /* Test all the array values could be parsed into double correctly */
  g_assert_nonnull (v);

  /* Add the value to the total */
  *total += *v;
}

static void
string_array_for_each (const char *v, gpointer user_data)
{
  char *buffer = user_data;
  g_assert_nonnull (buffer);

  /* Test all the array values could be parsed into strings correctly */
  g_assert_nonnull (v);

  /* Concatenate */
  g_strlcat(buffer, v, 256);
}

static void
unparsable_int64_array_for_each (const int64_t *v, gpointer user_data)
{
  /* Make sure the value is null */
  g_assert_null (v);
}

static void
test_basic_array (void)
{
  /* Parse the file and get its table */
  g_autoptr (WpTomlFile) file = wp_toml_file_new (TOML_FILE_BASIC_ARRAY);
  g_assert_nonnull (file);
  g_autoptr (WpTomlTable) table = wp_toml_file_get_table (file);
  g_assert_nonnull (table);

  /* Test bool array */
  {
    g_autoptr (WpTomlArray) a = wp_toml_table_get_array (table, "bool-array");
    g_assert_nonnull (a);
    int64_t total_trues = 0;
    wp_toml_array_for_each_boolean (a, boolean_array_for_each, &total_trues);
    g_assert_cmpuint (total_trues, ==, 2);
  }

  /* Test int64 array */
  {
    g_autoptr (WpTomlArray) a = wp_toml_table_get_array (table, "int64-array");
    g_assert_nonnull (a);
    int64_t total = 0;
    wp_toml_array_for_each_int64 (a, int64_array_for_each, &total);
    g_assert_cmpuint (total, ==, 15);
  }

  /* Test double array */
  {
    g_autoptr (WpTomlArray) a = wp_toml_table_get_array (table, "double-array");
    g_assert_nonnull (a);
    double total = 0;
    wp_toml_array_for_each_double (a, double_array_for_each, &total);
    g_assert_cmpfloat_with_epsilon (total, 3.3, 0.01);
  }

  /* Test string array */
  {
    g_autoptr (WpTomlArray) a = wp_toml_table_get_array (table, "str-array");
    g_assert_nonnull (a);
    char buffer[256] = "";
    wp_toml_array_for_each_string (a, string_array_for_each, &buffer);
    g_assert_cmpstr (buffer, ==, "a string array");
  }

  /* Try to parse a string array as an int64 array */
  {
    g_autoptr (WpTomlArray) a = wp_toml_table_get_array (table, "str-array");
    g_assert_nonnull (a);
    wp_toml_array_for_each_int64 (a, unparsable_int64_array_for_each, NULL);
  }
}

static void
test_nested_table (void)
{
  /* Parse the file and get its table */
  g_autoptr (WpTomlFile) file = wp_toml_file_new (TOML_FILE_NESTED_TABLE);
  g_assert_nonnull (file);
  g_autoptr (WpTomlTable) table = wp_toml_file_get_table (file);
  g_assert_nonnull (table);

  /* Get the first nested table */
  g_autoptr (WpTomlTable) table1 = wp_toml_table_get_table (table, "table");
  g_assert_nonnull (table1);

  /* Get the key1 and key2 values of the first nested table */
  double key1 = 0;
  wp_toml_table_get_double (table1, "key1", &key1);
  g_assert_cmpfloat_with_epsilon (key1, 0.1, 0.01);
  int32_t key2 = 0;
  wp_toml_table_get_int32 (table1, "key2", &key2);
  g_assert_cmpint (key2, ==, 1284);

  /* Get the second nested table */
  g_autoptr (WpTomlTable) table2 = wp_toml_table_get_table (table1, "subtable");
  g_assert_nonnull (table2);

  /* Get the key3 value of the second nested table */
  g_autofree char *key3 = wp_toml_table_get_string (table2, "key3");
  g_assert_nonnull (key3);
  g_assert_cmpstr (key3, ==, "hello world");
}

static void
nested_array_for_each (WpTomlArray *a, gpointer user_data)
{
  int *count = user_data;
  g_assert_nonnull (count);

  /* Test all the array values could be parsed into arrays correctly */
  g_assert_nonnull (a);

  /* Parse the nested arrays for each type */
  switch (*count) {
  case 0: {
    int64_t total = 0;
    wp_toml_array_for_each_int64 (a, int64_array_for_each, &total);
    g_assert_cmpint (total, ==, 15);
    break;
  }
  case 1: {
    char buffer[256] = "";
    wp_toml_array_for_each_string (a, string_array_for_each, &buffer);
    g_assert_cmpstr (buffer, ==, "helloworld");
    break;
  }
  case 2: {
    double total = 0;
    wp_toml_array_for_each_double (a, double_array_for_each, &total);
    g_assert_cmpfloat_with_epsilon (total, 3.3, 0.01);
    break;
  }
  default:
    break;
  }

  /* Increase the counter */
  (*count)++;
}

static void
test_nested_array ()
{
  /* Parse the file and get its table */
  g_autoptr (WpTomlFile) file = wp_toml_file_new (TOML_FILE_NESTED_ARRAY);
  g_assert_nonnull (file);
  g_autoptr (WpTomlTable) table = wp_toml_file_get_table (file);
  g_assert_nonnull (table);

  /* Test nested array */
  g_autoptr (WpTomlArray) a = wp_toml_table_get_array (table, "nested-array");
  g_assert_nonnull (a);
  int count = 0;
  wp_toml_array_for_each_array (a, nested_array_for_each, &count);
  g_assert_cmpint (count, ==, 3);
}

static void
table_array_for_each (const WpTomlTable *table, gpointer user_data)
{
  char *buffer = user_data;

  /* Test all the array values could be parsed into a table correctly */
  g_assert_nonnull (table);

  /* Check for key1 string */
  g_autofree char *key1 = wp_toml_table_get_string (table, "key1");
  g_assert_nonnull (key1);

  /* Concatenate */
  g_strlcat(buffer, key1, 256);
}

static void
test_table_array ()
{
  /* Parse the file and get its table */
  g_autoptr (WpTomlFile) file = wp_toml_file_new (TOML_FILE_TABLE_ARRAY);
  g_assert_nonnull (file);
  g_autoptr (WpTomlTable) table = wp_toml_file_get_table (file);
  g_assert_nonnull (table);

  /* Get the table array */
  g_autoptr (WpTomlTableArray) table_array = wp_toml_table_get_array_table (
      table, "table-array");
  g_assert_nonnull (table_array);

  /* Iterate */
  char buffer[256] = "";
  wp_toml_table_array_for_each (table_array, table_array_for_each, buffer);
  g_assert_cmpstr (buffer, ==, "hello, can you hear me?");
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/wptoml/basic_table", test_basic_table);
  g_test_add_func ("/wptoml/basic_array", test_basic_array);
  g_test_add_func ("/wptoml/nested_table", test_nested_table);
  g_test_add_func ("/wptoml/nested_array", test_nested_array);
  g_test_add_func ("/wptoml/table_array", test_table_array);

  return g_test_run ();
}
