/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

static void
test_spa_pod_basic (void)
{
  /* None */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_none ();
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_none (pod));
    g_assert_false (wp_spa_pod_is_id (pod));
    g_assert_cmpstr ("Spa:None", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_none ();
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Boolean */
  {
    g_autoptr (WpSpaPod) copy = NULL;
    g_assert_null (copy);

    {
      g_autoptr (WpSpaPod) pod = wp_spa_pod_new_boolean (TRUE);
      g_assert_nonnull (pod);
      g_assert_true (wp_spa_pod_is_boolean (pod));
      gboolean value = FALSE;
      g_assert_true (wp_spa_pod_get_boolean (pod, &value));
      g_assert_true (value);
      g_assert_cmpstr ("Spa:Bool", ==,
          wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
      g_assert_true (wp_spa_pod_set_boolean (pod, FALSE));
      g_assert_true (wp_spa_pod_get_boolean (pod, &value));
      g_assert_false (value);

      copy = wp_spa_pod_copy (pod);
    }

    g_assert_nonnull (copy);
    g_assert_true (wp_spa_pod_is_boolean (copy));
    gboolean value = FALSE;
    g_assert_true (wp_spa_pod_get_boolean (copy, &value));
    g_assert_false (value);
    g_assert_cmpstr ("Spa:Bool", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (copy)));
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_boolean (TRUE);
    g_assert_true (wp_spa_pod_set_pod (copy, other));
    g_assert_true (wp_spa_pod_get_boolean (copy, &value));
    g_assert_true (value);
    g_assert_true (wp_spa_pod_equal (copy, other));
  }

  /* Id */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_id (5);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_id (pod));
    guint32 value = 0;
    g_assert_true (wp_spa_pod_get_id (pod, &value));
    g_assert_cmpuint (value, ==, 5);
    g_assert_cmpstr ("Spa:Id", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_set_id (pod, 10));
    g_assert_true (wp_spa_pod_get_id (pod, &value));
    g_assert_cmpuint (value, ==, 10);
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_id (20);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_id (pod, &value));
    g_assert_cmpuint (value, ==, 20);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Int */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_int (-12);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_int (pod));
    gint32 value = 0;
    g_assert_true (wp_spa_pod_get_int (pod, &value));
    g_assert_cmpint (value, ==, -12);
    g_assert_cmpstr ("Spa:Int", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_set_int (pod, 9999));
    g_assert_true (wp_spa_pod_get_int (pod, &value));
    g_assert_cmpint (value, ==, 9999);
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_int (1000);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_int (pod, &value));
    g_assert_cmpuint (value, ==, 1000);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Long */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_long (LONG_MAX);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_long (pod));
    gint64 value = 0;
    g_assert_true (wp_spa_pod_get_long (pod, &value));
    g_assert_cmpint (value, ==, LONG_MAX);
    g_assert_cmpstr ("Spa:Long", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_set_long (pod, LONG_MIN));
    g_assert_true (wp_spa_pod_get_long (pod, &value));
    g_assert_cmpuint (value, ==, LONG_MIN);
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_long (0);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_long (pod, &value));
    g_assert_cmpuint (value, ==, 0);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Float */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_float (3.14);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_float (pod));
    float value = 0;
    g_assert_true (wp_spa_pod_get_float (pod, &value));
    g_assert_cmpfloat_with_epsilon (value, 3.14, 0.001);
    g_assert_cmpstr ("Spa:Float", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_set_float (pod, 1.0));
    g_assert_true (wp_spa_pod_get_float (pod, &value));
    g_assert_cmpfloat_with_epsilon (value, 1.0, 0.001);
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_float (-3.14);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_float (pod, &value));
    g_assert_cmpfloat_with_epsilon (value, -3.14, 0.001);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Double */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_double (2.718281828);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_double (pod));
    double value = 0;
    g_assert_true (wp_spa_pod_get_double (pod, &value));
    g_assert_cmpfloat_with_epsilon (value, 2.718281828, 0.0000000001);
    g_assert_cmpstr ("Spa:Double", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_set_double (pod, 2.0));
    g_assert_true (wp_spa_pod_get_double (pod, &value));
    g_assert_cmpfloat_with_epsilon (value, 2.0, 0.0000000001);
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_double (3.0);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_double (pod, &value));
    g_assert_cmpfloat_with_epsilon (value, 3.0, 0.0000000001);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* String */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_string ("WirePlumber");
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_string (pod));
    const char *value = NULL;
    g_assert_true (wp_spa_pod_get_string (pod, &value));
    g_assert_nonnull (value);
    g_assert_cmpstr (value, ==, "WirePlumber");
    g_assert_cmpstr ("Spa:String", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_string ("Other");
    g_assert_nonnull (other);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_string (pod, &value));
    g_assert_nonnull (value);
    g_assert_cmpstr (value, ==, "Other");
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Bytes */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_bytes ("bytes", 5);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_bytes (pod));
    gconstpointer value = NULL;
    guint32 len = 0;
    g_assert_true (wp_spa_pod_get_bytes (pod, &value, &len));
    g_assert_nonnull (value);
    g_assert_cmpmem (value, len, "bytes", 5);
    g_assert_cmpuint (len, ==, 5);
    g_assert_cmpstr ("Spa:Bytes", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_bytes ("pod", 3);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_bytes (pod, &value, &len));
    g_assert_nonnull (value);
    g_assert_cmpmem (value, len, "pod", 3);
    g_assert_cmpuint (len, ==, 3);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Pointer */
  {
    gint i = 3;
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_pointer ("Spa:Pointer:Buffer", &i);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_pointer (pod));
    gconstpointer p = NULL;
    g_assert_true (wp_spa_pod_get_pointer (pod, &p));
    g_assert_nonnull (p);
    g_assert_true (p == &i);
    g_assert_cmpint (*(gint *)p, ==, 3);
    g_assert_cmpstr ("Spa:Pointer:Buffer", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));

    float f = 1.1;
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_pointer ("Spa:Pointer:Meta", &f);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_pointer (pod, &p));
    g_assert_nonnull (p);
    g_assert_true (p == &f);
    g_assert_cmpfloat_with_epsilon (*(float *)p, 1.1, 0.01);
    g_assert_cmpstr ("Spa:Pointer:Meta", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Fd */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_fd (4);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_fd (pod));
    gint64 value = 0;
    g_assert_true (wp_spa_pod_get_fd (pod, &value));
    g_assert_cmpint (value, ==, 4);
    g_assert_cmpstr ("Spa:Fd", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_set_fd (pod, 1));
    g_assert_true (wp_spa_pod_get_fd (pod, &value));
    g_assert_cmpuint (value, ==, 1);
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_fd (10);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_fd (pod, &value));
    g_assert_cmpuint (value, ==, 10);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Rectangle */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_rectangle (1920, 1080);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_rectangle (pod));
    guint32 width = 0;
    guint32 height = 0;
    g_assert_true (wp_spa_pod_get_rectangle (pod, &width, &height));
    g_assert_cmpint (width, ==, 1920);
    g_assert_cmpint (height, ==, 1080);
    g_assert_cmpstr ("Spa:Rectangle", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_set_rectangle (pod, 640, 480));
    g_assert_true (wp_spa_pod_get_rectangle (pod, &width, &height));
    g_assert_cmpint (width, ==, 640);
    g_assert_cmpint (height, ==, 480);
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_rectangle (200, 100);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_rectangle (pod, &width, &height));
    g_assert_cmpint (width, ==, 200);
    g_assert_cmpint (height, ==, 100);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }

  /* Fraction */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_fraction (16, 9);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_fraction (pod));
    guint32 num = 0;
    guint32 denom = 0;
    g_assert_true (wp_spa_pod_get_fraction (pod, &num, &denom));
    g_assert_cmpint (num, ==, 16);
    g_assert_cmpint (denom, ==, 9);
    g_assert_cmpstr ("Spa:Fraction", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_true (wp_spa_pod_set_fraction (pod, 4, 3));
    g_assert_true (wp_spa_pod_get_fraction (pod, &num, &denom));
    g_assert_cmpint (num, ==, 4);
    g_assert_cmpint (denom, ==, 3);
    g_autoptr (WpSpaPod) other = wp_spa_pod_new_fraction (2, 1);
    g_assert_true (wp_spa_pod_set_pod (pod, other));
    g_assert_true (wp_spa_pod_get_fraction (pod, &num, &denom));
    g_assert_cmpint (num, ==, 2);
    g_assert_cmpint (denom, ==, 1);
    g_assert_true (wp_spa_pod_equal (pod, other));
  }
}

static void
test_spa_pod_choice (void)
{
  /* Static Enum */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_choice (
        "Enum", "i", 0, "i", 1, "i", 2, NULL);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_choice (pod));
    g_assert_cmpstr ("Spa:Pod:Choice", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    g_assert_cmpstr ("Enum", ==,
        wp_spa_id_value_short_name (wp_spa_pod_get_choice_type (pod)));

    g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (pod);
    g_assert_nonnull (child);
    g_assert_cmpstr ("Spa:Int", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (child)));
    gint32 value = 1;
    g_assert_true (wp_spa_pod_get_int (child, &value));
    g_assert_cmpint (value, ==, 0);
    g_assert_true (wp_spa_pod_set_int (child, 3));
    g_assert_true (wp_spa_pod_get_int (child, &value));
    g_assert_cmpint (value, ==, 3);
  }

  /* Static None */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_choice ("None", "s",
        "default value", NULL);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_choice (pod));
    g_assert_cmpstr ("Spa:Pod:Choice", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));

    {
      g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (pod);
      g_assert_nonnull (child);
      g_assert_cmpstr ("Spa:String", ==,
          wp_spa_type_name (wp_spa_pod_get_spa_type (child)));
      const char *value = NULL;
      g_assert_true (wp_spa_pod_get_string (child, &value));
      g_assert_nonnull (value);
      g_assert_cmpstr ("default value", ==, value);
      g_autoptr (WpSpaPod) str_pod = wp_spa_pod_new_string ("new value");
      g_assert_true (wp_spa_pod_set_pod (child, str_pod));
      g_assert_true (wp_spa_pod_get_string (child, &value));
      g_assert_cmpstr ("new value", ==, value);
    }

    {
      g_autoptr (WpSpaPod) child = wp_spa_pod_get_choice_child (pod);
      g_assert_nonnull (child);
      g_assert_cmpstr ("Spa:String", ==,
          wp_spa_type_name (wp_spa_pod_get_spa_type (child)));
      const char *value = NULL;
      g_assert_true (wp_spa_pod_get_string (child, &value));
      g_assert_nonnull (value);
      g_assert_cmpstr ("new value", ==, value);
    }
  }

  /* Dynamic */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_choice ("Enum");
    wp_spa_pod_builder_add (b, "i", 0, NULL);
    wp_spa_pod_builder_add (b, "i", 1, NULL);
    wp_spa_pod_builder_add (b, "i", 2, NULL);
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_choice (pod));
    g_assert_cmpstr ("Spa:Pod:Choice", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
  }

  /* It is not possible to use the parser to get the contents of a choice, you
   * need to use the iterator API to achieve that. This is because there is no
   * `spa_pod_parser_get_choice` API in the SPA library */
}

static void
test_spa_pod_array (void)
{
  /* Dynamic */
  {
    WpSpaPodBuilder *b = wp_spa_pod_builder_new_array ();
    wp_spa_pod_builder_add (b, "b", FALSE, NULL);
    wp_spa_pod_builder_add (b, "b", TRUE, NULL);
    wp_spa_pod_builder_add (b, "b", TRUE, NULL);
    wp_spa_pod_builder_add (b, "b", FALSE, NULL);
    wp_spa_pod_builder_add (b, "b", TRUE, NULL);
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_array (pod));
    g_assert_cmpstr ("Spa:Array", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
    wp_spa_pod_builder_unref (b);
    g_assert_true (wp_spa_pod_is_array (pod));

    g_autoptr (WpSpaPod) child = wp_spa_pod_get_array_child (pod);
    g_assert_nonnull (child);
    g_assert_cmpstr ("Spa:Bool", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (child)));
    gboolean value = TRUE;
    g_assert_true (wp_spa_pod_get_boolean (child, &value));
    g_assert_false (value);
  }

  /* It is not possible to use the parser to get the contents of an array, you
   * need to use the iterator API to achieve that. This is because there is no
   * `spa_pod_parser_get_array` API in the SPA library. */
}

static void
test_spa_pod_object (void)
{
  /* Static */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_object (
        "Spa:Pod:Object:Param:Props", "Props",
        "mute", "b", FALSE,
        "volume", "f", 0.5,
        "frequency", "i", 440,
        "device", "s", "device-name",
        "deviceFd", "h", 5,
        NULL);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_object (pod));
    g_assert_cmpstr ("Spa:Pod:Object:Param:Props", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));

    const char *id_name;
    gboolean mute = TRUE;
    float vol = 0.0;
    gint32 frequency;
    const char *device;
    gint64 device_fd;
    g_assert_true (wp_spa_pod_get_object (pod,
        &id_name,
        "mute", "b", &mute,
        "volume", "f", &vol,
        "frequency", "i", &frequency,
        "device", "s", &device,
        "deviceFd", "h", &device_fd,
        NULL));
    g_assert_cmpstr (id_name, ==, "Props");
    g_assert_false (mute);
    g_assert_cmpfloat_with_epsilon (vol, 0.5, 0.01);
    g_assert_cmpint (frequency, ==, 440);
    g_assert_cmpstr (device, ==, "device-name");
    g_assert_cmpint (device_fd, ==, 5);
  }

  /* Dynamic */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_object (
        "Spa:Pod:Object:Param:Props", "Props");
    wp_spa_pod_builder_add_property (b, "mute");
    wp_spa_pod_builder_add_boolean (b, FALSE);
    wp_spa_pod_builder_add_property (b, "volume");
    wp_spa_pod_builder_add_float (b, 0.5);
    wp_spa_pod_builder_add_property (b, "frequency");
    wp_spa_pod_builder_add_int (b, 440);
    wp_spa_pod_builder_add_property (b, "device");
    wp_spa_pod_builder_add_string (b, "device-name");
    wp_spa_pod_builder_add_property (b, "deviceFd");
    wp_spa_pod_builder_add_fd (b, 5);
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_object (pod));
    g_assert_cmpstr ("Spa:Pod:Object:Param:Props", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));

    const char *id_name;
    gboolean mute = TRUE;
    float vol = 0.0;
    gint32 frequency;
    const char *device;
    gint64 device_fd;
    g_autoptr (WpSpaPodParser) p = wp_spa_pod_parser_new_object (pod, &id_name);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_parser_get (p, "mute", "b", &mute, NULL));
    g_assert_true (wp_spa_pod_parser_get (p, "volume", "f", &vol, NULL));
    g_assert_true (wp_spa_pod_parser_get (p, "frequency", "i", &frequency, NULL));
    g_assert_true (wp_spa_pod_parser_get (p, "device", "s", &device, NULL));
    g_assert_true (wp_spa_pod_parser_get (p, "deviceFd", "h", &device_fd, NULL));
    wp_spa_pod_parser_end (p);
    g_assert_cmpstr (id_name, ==, "Props");
    g_assert_false (mute);
    g_assert_cmpfloat_with_epsilon (vol, 0.5, 0.01);
    g_assert_cmpint (frequency, ==, 440);
    g_assert_cmpstr (device, ==, "device-name");
    g_assert_cmpint (device_fd, ==, 5);
  }
}

static void
test_spa_pod_struct (void)
{
  /* Dynamic */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_struct ();
    wp_spa_pod_builder_add_boolean (b, TRUE);
    wp_spa_pod_builder_add_id (b, 2);
    wp_spa_pod_builder_add_int (b, 8);
    wp_spa_pod_builder_add_long (b, 64);
    wp_spa_pod_builder_add_float (b, 3.14);
    wp_spa_pod_builder_add_double (b, 2.718281828);
    wp_spa_pod_builder_add_string (b, "WirePlumber");
    wp_spa_pod_builder_add_bytes (b, "bytes", 5);
    wp_spa_pod_builder_add_pointer (b, "Spa:Pointer:Buffer", b);
    wp_spa_pod_builder_add_fd (b, 4);
    wp_spa_pod_builder_add_rectangle (b, 1920, 1080);
    wp_spa_pod_builder_add_fraction (b, 16, 9);
    {
      g_autoptr (WpSpaPod) pod = wp_spa_pod_new_int (35254);
      wp_spa_pod_builder_add_pod (b, pod);
    }
    {
      g_autoptr (WpSpaPod) pod = wp_spa_pod_new_object (
        "Spa:Pod:Object:Param:Props", "Props",
        "mute", "b", FALSE,
        NULL);
      wp_spa_pod_builder_add (b, "P", pod, NULL);
    }
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_struct (pod));
    g_assert_cmpstr ("Spa:Pod:Struct", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));

    g_autoptr (WpSpaPodParser) p = wp_spa_pod_parser_new_struct (pod);
    g_assert_nonnull (pod);

    gboolean value_boolean;
    g_assert_true (wp_spa_pod_parser_get_boolean (p, &value_boolean));
    g_assert_true (value_boolean);

    guint32 value_id;
    g_assert_true (wp_spa_pod_parser_get_id (p, &value_id));
    g_assert_cmpuint (value_id, ==, 2);

    gint32 value_int;
    g_assert_true (wp_spa_pod_parser_get_int (p, &value_int));
    g_assert_cmpint (value_int, ==, 8);

    gint64 value_long;
    g_assert_true (wp_spa_pod_parser_get_long (p, &value_long));
    g_assert_cmpint (value_long, ==, 64);

    float value_float;
    g_assert_true (wp_spa_pod_parser_get_float (p, &value_float));
    g_assert_cmpfloat_with_epsilon (value_float, 3.14, 0.001);

    double value_double;
    g_assert_true (wp_spa_pod_parser_get_double (p, &value_double));
    g_assert_cmpfloat_with_epsilon (value_double, 2.718281828, 0.0000000001);

    const char *value_string;
    g_assert_true (wp_spa_pod_parser_get_string (p, &value_string));
    g_assert_cmpstr (value_string, ==, "WirePlumber");

    gconstpointer value_bytes;
    guint32 len_bytes;
    g_assert_true (wp_spa_pod_parser_get_bytes (p, &value_bytes, &len_bytes));
    g_assert_cmpmem (value_bytes, len_bytes, "bytes", 5);
    g_assert_cmpuint (len_bytes, ==, 5);

    gconstpointer value_pointer;
    g_assert_true (wp_spa_pod_parser_get_pointer (p, &value_pointer));
    g_assert_nonnull (value_pointer);
    g_assert_true (value_pointer == b);

    gint64 value_fd;
    g_assert_true (wp_spa_pod_parser_get_fd (p, &value_fd));
    g_assert_cmpint (value_fd, ==, 4);

    guint32 value_width;
    guint32 value_height;
    g_assert_true (wp_spa_pod_parser_get_rectangle (p, &value_width, &value_height));
    g_assert_cmpuint (value_width, ==, 1920);
    g_assert_cmpuint (value_height, ==, 1080);

    guint32 value_num;
    guint32 value_denom;
    g_assert_true (wp_spa_pod_parser_get_fraction (p, &value_num, &value_denom));
    g_assert_cmpuint (value_num, ==, 16);
    g_assert_cmpuint (value_denom, ==, 9);

    g_autoptr (WpSpaPod) value_pod = wp_spa_pod_parser_get_pod (p);
    g_assert_nonnull (value_pod);
    gint value_pod_int;
    g_assert_true (wp_spa_pod_get_int (value_pod, &value_pod_int));
    g_assert_cmpint (value_pod_int, ==, 35254);

    g_autoptr (WpSpaPod) value_object = NULL;
    g_assert_true (wp_spa_pod_parser_get (p, "P", &value_object, NULL));
    g_assert_nonnull (value_object);
    const char *id_name;
    gboolean mute = TRUE;

    g_assert_true (wp_spa_pod_get_object (value_object,
        &id_name,
        "mute", "b", &mute,
        NULL));
    g_assert_cmpstr (id_name, ==, "Props");
    g_assert_false (mute);
  }
}

static void
test_spa_pod_sequence (void)
{
  /* Static */
  {
    g_autoptr (WpSpaPod) pod = wp_spa_pod_new_sequence (0,
        10, "Properties", "l", 9999, NULL);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_sequence (pod));
    g_assert_cmpstr ("Spa:Pod:Sequence", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
  }

  /* Dynamic */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_sequence (0);
    wp_spa_pod_builder_add_control (b, 10, "Properties");
    wp_spa_pod_builder_add_long (b, 9999);
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);
    g_assert_true (wp_spa_pod_is_sequence (pod));
    g_assert_cmpstr ("Spa:Pod:Sequence", ==,
        wp_spa_type_name (wp_spa_pod_get_spa_type (pod)));
  }

  /* It is not possible to use the parser to get the contents of a sequence, you
   * need to use the iterator API to achieve that. This is because there is no
   * `spa_pod_parser_get_sequence` API in the SPA library. */
}

static void
choice_foreach (const GValue *item, gpointer data)
{
  gint32 *total = data;
  const gint32 *value = g_value_get_pointer (item);
  *total += *value;
}

static void
array_foreach (const GValue *item, gpointer data)
{
  gint32 *total = data;
  const gint32 *value = g_value_get_pointer (item);
  *total += *value;
}

static void
object_foreach (const GValue *item, gpointer data)
{
  guint32 *total_props = data;
  WpSpaPod *prop = g_value_get_boxed (item);
  g_assert_true (wp_spa_pod_is_property (prop));
  *total_props += 1;
}

static void
struct_foreach (const GValue *item, gpointer data)
{
  guint32 *total_fields = data;
  *total_fields += 1;
}

static void
sequence_foreach (const GValue *item, gpointer data)
{
  guint32 *offset_total = data;
  WpSpaPod *control = g_value_get_boxed (item);
  g_assert_true (wp_spa_pod_is_control (control));
  guint32 offset = 0;
  g_assert_true (wp_spa_pod_get_control (control, &offset, NULL, NULL));
  *offset_total += offset;
}

static void
test_spa_pod_iterator (void)
{
  /* Choice */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_choice ("Enum");
    wp_spa_pod_builder_add (b, "i", 0, NULL);
    wp_spa_pod_builder_add (b, "i", 1, NULL);
    wp_spa_pod_builder_add (b, "i", 2, NULL);
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);

    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    g_assert_nonnull (it);

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      gpointer p = g_value_get_pointer (&next);
      g_assert_nonnull (p);
      g_assert_cmpint (*(gint *)p, ==, 0);
      g_value_unset (&next);
    }

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      gpointer p = g_value_get_pointer (&next);
      g_assert_nonnull (p);
      g_assert_cmpint (*(gint *)p, ==, 1);
      g_value_unset (&next);
    }

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      gpointer p = g_value_get_pointer (&next);
      g_assert_nonnull (p);
      g_assert_cmpint (*(gint *)p, ==, 2);
      g_value_unset (&next);
    }

    {
      g_assert_false (wp_iterator_next (it, NULL));
    }

    gint32 total = 0;
    g_assert_true (wp_iterator_foreach (it, choice_foreach, &total));
    g_assert_cmpint (total, ==, 3);

  }

  /* Array */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_array ();
    wp_spa_pod_builder_add_int (b, 1);
    wp_spa_pod_builder_add_int (b, 2);
    wp_spa_pod_builder_add_int (b, 3);
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);

    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    g_assert_nonnull (it);

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      gpointer p = g_value_get_pointer (&next);
      g_assert_nonnull (p);
      g_assert_cmpint (*(gint *)p, ==, 1);
      g_value_unset (&next);
    }

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      gpointer p = g_value_get_pointer (&next);
      g_assert_nonnull (p);
      g_assert_cmpint (*(gint *)p, ==, 2);
      g_value_unset (&next);
    }

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      gpointer p = g_value_get_pointer (&next);
      g_assert_nonnull (p);
      g_assert_cmpint (*(gint *)p, ==, 3);
      g_value_unset (&next);
    }

    {
      g_assert_false (wp_iterator_next (it, NULL));
    }

    gint total = 0;
    g_assert_true (wp_iterator_foreach (it, array_foreach, &total));
    g_assert_cmpint (total, ==, 6);
  }

  /* Object */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_object (
        "Spa:Pod:Object:Param:Props", "Props");
    wp_spa_pod_builder_add_property (b, "mute");
    wp_spa_pod_builder_add_boolean (b, FALSE);
    wp_spa_pod_builder_add_property (b, "device");
    wp_spa_pod_builder_add_string (b, "device-name");
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);

    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    g_assert_nonnull (it);

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      WpSpaPod *p = g_value_get_boxed (&next);
      g_assert_nonnull (p);
      g_assert_true (wp_spa_pod_is_property (p));
      const char *key = NULL;
      g_autoptr (WpSpaPod) value = NULL;
      g_assert_true (wp_spa_pod_get_property (p, &key, &value));
      g_assert_cmpstr (key, ==, "mute");
      gboolean b = TRUE;
      g_assert_true (wp_spa_pod_get_boolean (value, &b));
      g_assert_false (b);
      g_value_unset (&next);
    }

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      WpSpaPod *p = g_value_get_boxed (&next);
      g_assert_nonnull (p);
      g_assert_true (wp_spa_pod_is_property (p));
      const char *key = NULL;
      g_autoptr (WpSpaPod) value = NULL;
      g_assert_true (wp_spa_pod_get_property (p, &key, &value));
      g_assert_cmpstr (key, ==, "device");
      const char *s = NULL;
      g_assert_true (wp_spa_pod_get_string (value, &s));
      g_assert_cmpstr (s, ==, "device-name");
      g_value_unset (&next);
    }

    {
      g_assert_false (wp_iterator_next (it, NULL));
    }

    guint32 total_props = 0;
    g_assert_true (wp_iterator_foreach (it, object_foreach, &total_props));
    g_assert_cmpuint (total_props, ==, 2);
  }

  /* Struct */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_struct ();
    wp_spa_pod_builder_add_boolean (b, TRUE);
    wp_spa_pod_builder_add_id (b, 2);
    wp_spa_pod_builder_add_int (b, 8);
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);

    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    g_assert_nonnull (it);

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      WpSpaPod *p = g_value_get_boxed (&next);
      g_assert_nonnull (p);
      gboolean v = FALSE;
      g_assert_true (wp_spa_pod_get_boolean (p, &v));
      g_assert_true (v);
      g_value_unset (&next);
    }

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      WpSpaPod *p = g_value_get_boxed (&next);
      g_assert_nonnull (p);
      guint32 v = 0;
      g_assert_true (wp_spa_pod_get_id (p, &v));
      g_assert_cmpuint (v, ==, 2);
      g_value_unset (&next);
    }

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      WpSpaPod *p = g_value_get_boxed (&next);
      g_assert_nonnull (p);
      gint v = 0;
      g_assert_true (wp_spa_pod_get_int (p, &v));
      g_assert_cmpint (v, ==, 8);
      g_value_unset (&next);
    }

    {
      g_assert_false (wp_iterator_next (it, NULL));
    }

    guint32 total_fields = 0;
    g_assert_true (wp_iterator_foreach (it, struct_foreach, &total_fields));
    g_assert_cmpuint (total_fields, ==, 3);
  }

  /* Sequence */
  {
    g_autoptr (WpSpaPodBuilder) b = wp_spa_pod_builder_new_sequence (0);
    wp_spa_pod_builder_add_control (b, 10, "Properties");
    wp_spa_pod_builder_add_float (b, 0.33);
    wp_spa_pod_builder_add_control (b, 40, "Properties");
    wp_spa_pod_builder_add_float (b, 0.66);
    g_autoptr (WpSpaPod) pod = wp_spa_pod_builder_end (b);
    g_assert_nonnull (pod);

    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    g_assert_nonnull (it);

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      WpSpaPod *p = g_value_get_boxed (&next);
      g_assert_nonnull (p);
      g_assert_true (wp_spa_pod_is_control (p));
      guint32 offset = 0;
      const char *type_name = NULL;
      g_autoptr (WpSpaPod) value = NULL;
      g_assert_true (wp_spa_pod_get_control (p, &offset, &type_name, &value));
      g_assert_cmpuint (offset, ==, 10);
      g_assert_cmpstr (type_name, ==, "Properties");
      float f = 0;
      g_assert_true (wp_spa_pod_get_float (value, &f));
      g_assert_cmpfloat_with_epsilon (f, 0.33, 0.001);
      g_value_unset (&next);
    }

    {
      GValue next = G_VALUE_INIT;
      g_assert_true (wp_iterator_next (it, &next));
      WpSpaPod *p = g_value_get_boxed (&next);
      g_assert_nonnull (p);
      g_assert_true (wp_spa_pod_is_control (p));
      guint32 offset = 0;
      const char *type_name = NULL;
      g_autoptr (WpSpaPod) value = NULL;
      g_assert_true (wp_spa_pod_get_control (p, &offset, &type_name, &value));
      g_assert_cmpuint (offset, ==, 40);
      g_assert_cmpstr (type_name, ==, "Properties");
      float f = 0;
      g_assert_true (wp_spa_pod_get_float (value, &f));
      g_assert_cmpfloat_with_epsilon (f, 0.66, 0.001);
      g_value_unset (&next);
    }

    {
      g_assert_false (wp_iterator_next (it, NULL));
    }

    guint32 offset_total = 0;
    g_assert_true (wp_iterator_foreach (it, sequence_foreach, &offset_total));
    g_assert_cmpuint (offset_total, ==, 50);
  }
}

static void
test_spa_pod_unique_owner (void)
{
  /* Create an object */
  WpSpaPod *pod = wp_spa_pod_new_object (
        "Spa:Pod:Object:Param:PropInfo", "PropInfo",
        "id", "K", "unknown",
        "name", "s", "prop-info-name",
        NULL);
  g_assert_nonnull (pod);
  g_assert_true (wp_spa_pod_is_unique_owner (pod));

  /* Get the first property using an iterator */
  GValue next = G_VALUE_INIT;
  g_autoptr (WpSpaPod) property = NULL;
  {
    g_autoptr (WpIterator) it = wp_spa_pod_new_iterator (pod);
    g_assert_nonnull (it);
    g_assert_true (wp_iterator_next (it, &next));
    property = g_value_dup_boxed (&next);
  }
  g_assert_nonnull (property);
  g_assert_true (wp_spa_pod_is_property (property));
  {
    g_autoptr (WpSpaPod) value = NULL;
    const char *key = NULL;
    g_assert_true (wp_spa_pod_get_property (property, &key, &value));
    g_assert_nonnull (key);
    g_assert_cmpstr (key, ==, "id");
    g_assert_nonnull (value);
    guint32 id = 0;
    g_assert_true (wp_spa_pod_get_id (value, &id));
    g_assert_cmpuint (id, ==, 1);
  }

  /* Own the data */
  g_assert_true (wp_spa_pod_is_unique_owner (pod));
  g_assert_false (wp_spa_pod_is_unique_owner (property));
  property = wp_spa_pod_ensure_unique_owner (property);
  g_assert_true (wp_spa_pod_is_unique_owner (pod));
  g_assert_true (wp_spa_pod_is_unique_owner (property));

  /* Destroy the object */
  wp_spa_pod_unref (pod);
  g_assert_true (wp_spa_pod_is_unique_owner (property));

  /* Make sure the property data is still valid */
  {
    g_autoptr (WpSpaPod) value = NULL;
    const char *key = NULL;
    g_assert_true (wp_spa_pod_get_property (property, &key, &value));
    g_assert_nonnull (key);
    g_assert_cmpstr (key, ==, "id");
    g_assert_nonnull (value);
    guint32 id = 0;
    g_assert_true (wp_spa_pod_get_id (value, &id));
    g_assert_cmpuint (id, ==, 1);
  }

  /* Destroy the property */
  g_value_unset (&next);
}

static void
test_spa_pod_port_config (void)
{
  const gint32 rate = 48000;
  const gint32 channels = 2;

  /* Build the format to make sure the types exist */
  g_autoptr (WpSpaPodBuilder) builder = wp_spa_pod_builder_new_object (
     "Spa:Pod:Object:Param:Format", "Format");
  wp_spa_pod_builder_add (builder,
     "mediaType",    "K", "audio",
     "mediaSubtype", "K", "raw",
     "format",       "K", "S16LE",
     "rate",         "i", rate,
     "channels",     "i", channels,
     NULL);
  g_autoptr (WpSpaPodBuilder) position_builder = wp_spa_pod_builder_new_array ();
  for (guint i = 0; i < channels; i++)
    wp_spa_pod_builder_add_id (position_builder, 0);
  wp_spa_pod_builder_add_property (builder, "position");
  g_autoptr (WpSpaPod) position = wp_spa_pod_builder_end (position_builder);
  wp_spa_pod_builder_add_pod (builder, position);
  g_autoptr (WpSpaPod) format = wp_spa_pod_builder_end (builder);
  g_assert_nonnull (format);

  /* Build the port config to make sure the types exist */
  g_autoptr (WpSpaPod) pod = wp_spa_pod_new_object (
      "Spa:Pod:Object:Param:PortConfig", "PortConfig",
      "direction",  "K", "Input",
      "mode",       "K", "dsp",
      "monitor",    "b", FALSE,
      "control",    "b", FALSE,
      "format",     "P", format,
      NULL);
  g_assert_nonnull (pod);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/wp/spa-pod/basic", test_spa_pod_basic);
  g_test_add_func ("/wp/spa-pod/choice", test_spa_pod_choice);
  g_test_add_func ("/wp/spa-pod/array", test_spa_pod_array);
  g_test_add_func ("/wp/spa-pod/object", test_spa_pod_object);
  g_test_add_func ("/wp/spa-pod/struct", test_spa_pod_struct);
  g_test_add_func ("/wp/spa-pod/sequence", test_spa_pod_sequence);
  g_test_add_func ("/wp/spa-pod/iterator", test_spa_pod_iterator);
  g_test_add_func ("/wp/spa-pod/unique-owner", test_spa_pod_unique_owner);
  g_test_add_func ("/wp/spa-pod/port-config", test_spa_pod_port_config);

  return g_test_run ();
}
