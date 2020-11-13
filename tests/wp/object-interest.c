/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>

enum {
  PROP_0,
  PROP_TEST_STRING,
  PROP_TEST_INT,
  PROP_TEST_UINT,
  PROP_TEST_INT64,
  PROP_TEST_UINT64,
  PROP_TEST_FLOAT,
  PROP_TEST_DOUBLE,
  PROP_TEST_BOOLEAN,
};

struct _TestObjA
{
  GObject parent;
  gchar *test_string;
  gint test_int;
  guint test_uint;
  gint64 test_int64;
  guint64 test_uint64;
  gfloat test_float;
  gdouble test_double;
  gboolean test_boolean;
};

#define TEST_TYPE_A (test_obj_a_get_type ())
G_DECLARE_FINAL_TYPE (TestObjA, test_obj_a, TEST, OBJ_A, GObject)
G_DEFINE_TYPE (TestObjA, test_obj_a, G_TYPE_OBJECT)

static void
test_obj_a_init (TestObjA * self)
{
}

static void
test_obj_a_finalize (GObject * object)
{
  TestObjA *self = TEST_OBJ_A (object);
  g_free (self->test_string);
  G_OBJECT_CLASS (test_obj_a_parent_class)->finalize (object);
}

static void
test_obj_a_get_property (GObject * object, guint id, GValue * value,
    GParamSpec * pspec)
{
  TestObjA *self = TEST_OBJ_A (object);

  switch (id) {
    case PROP_TEST_STRING:
      g_value_set_string (value, self->test_string);
      break;
    case PROP_TEST_INT:
      g_value_set_int (value, self->test_int);
      break;
    case PROP_TEST_UINT:
      g_value_set_uint (value, self->test_uint);
      break;
    case PROP_TEST_INT64:
      g_value_set_int64 (value, self->test_int64);
      break;
    case PROP_TEST_UINT64:
      g_value_set_uint64 (value, self->test_uint64);
      break;
    case PROP_TEST_FLOAT:
      g_value_set_float (value, self->test_float);
      break;
    case PROP_TEST_DOUBLE:
      g_value_set_double (value, self->test_double);
      break;
    case PROP_TEST_BOOLEAN:
      g_value_set_boolean (value, self->test_boolean);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
      break;
  }
}

static void
test_obj_a_set_property (GObject * object, guint id, const GValue * value,
    GParamSpec * pspec)
{
  TestObjA *self = TEST_OBJ_A (object);

  switch (id) {
    case PROP_TEST_STRING:
      self->test_string = g_value_dup_string (value);
      break;
    case PROP_TEST_INT:
      self->test_int = g_value_get_int (value);
      break;
    case PROP_TEST_UINT:
      self->test_uint = g_value_get_uint (value);
      break;
    case PROP_TEST_INT64:
      self->test_int64 = g_value_get_int64 (value);
      break;
    case PROP_TEST_UINT64:
      self->test_uint64 = g_value_get_uint64 (value);
      break;
    case PROP_TEST_FLOAT:
      self->test_float = g_value_get_float (value);
      break;
    case PROP_TEST_DOUBLE:
      self->test_double = g_value_get_double (value);
      break;
    case PROP_TEST_BOOLEAN:
      self->test_boolean = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
      break;
  }
}

static void
test_obj_a_class_init (TestObjAClass * klass)
{
  GObjectClass *obj_class = (GObjectClass *) klass;

  obj_class->finalize = test_obj_a_finalize;
  obj_class->get_property = test_obj_a_get_property;
  obj_class->set_property = test_obj_a_set_property;

  g_object_class_install_property (obj_class, PROP_TEST_STRING,
      g_param_spec_string ("test-string", "test-string", "blurb", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TEST_INT,
      g_param_spec_int ("test-int", "test-int", "blurb",
          G_MININT, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TEST_UINT,
      g_param_spec_uint ("test-uint", "test-uint", "blurb",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TEST_INT64,
      g_param_spec_int64 ("test-int64", "test-int64", "blurb",
          G_MININT64, G_MAXINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TEST_UINT64,
      g_param_spec_uint64 ("test-uint64", "test-uint64", "blurb",
          0, G_MAXUINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TEST_FLOAT,
      g_param_spec_float ("test-float", "test-float", "blurb",
          -20.0f, 20.0f, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TEST_DOUBLE,
      g_param_spec_double ("test-double", "test-double", "blurb",
          -20.0, 20.0, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TEST_BOOLEAN,
      g_param_spec_boolean ("test-boolean", "test-boolean", "blurb", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

struct _TestObjB
{
  TestObjA parent;
};

#define TEST_TYPE_B (test_obj_b_get_type ())
G_DECLARE_FINAL_TYPE (TestObjB, test_obj_b, TEST, OBJ_B, TestObjA)
G_DEFINE_TYPE (TestObjB, test_obj_b, TEST_TYPE_A)

static void
test_obj_b_init (TestObjB * self)
{
}

static void
test_obj_b_class_init (TestObjBClass * klass)
{
}

typedef struct {
  GObject *object;
} TestFixture;

static void
test_object_interest_setup (TestFixture * f, gconstpointer data)
{
  f->object = g_object_new (TEST_TYPE_B,
      "test-string", "toast",
      "test-int", -30,
      "test-uint", 50,
      "test-int64", G_GINT64_CONSTANT (-0x1d636b02300a7aa7),
      "test-uint64", G_GUINT64_CONSTANT (0x1d636b02300a7aa7),
      "test-float", 3.14f,
      "test-double", 3.1415926545897932384626433,
      "test-boolean", TRUE,
      NULL);
  g_assert_nonnull (f->object);
}

static void
test_object_interest_teardown (TestFixture * f, gconstpointer data)
{
  g_clear_object (&f->object);
}

#define TEST_EXPECT_MATCH(interest) \
  G_STMT_START { \
    g_autoptr (GError) error = NULL; \
    gboolean ret; \
    \
    g_assert_nonnull (interest); \
    \
    ret = wp_object_interest_validate (interest, &error); \
    g_assert_no_error (error); \
    g_assert_true (ret); \
    \
    g_assert_true (wp_object_interest_matches (interest, f->object)); \
    \
    g_clear_pointer (&interest, wp_object_interest_free); \
  } G_STMT_END

#define TEST_EXPECT_NO_MATCH(interest) \
  G_STMT_START { \
    g_autoptr (GError) error = NULL; \
    gboolean ret; \
    \
    g_assert_nonnull (interest); \
    \
    ret = wp_object_interest_validate (interest, &error); \
    g_assert_no_error (error); \
    g_assert_true (ret); \
    \
    g_assert_false (wp_object_interest_matches (interest, f->object)); \
    \
    g_clear_pointer (&interest, wp_object_interest_free); \
  } G_STMT_END

#define TEST_EXPECT_MATCH_WP_PROPS(interest, props, global_props) \
  G_STMT_START { \
    g_autoptr (GError) error = NULL; \
    gboolean ret; \
    \
    g_assert_nonnull (interest); \
    \
    ret = wp_object_interest_validate (interest, &error); \
    g_assert_no_error (error); \
    g_assert_true (ret); \
    \
    g_assert_true (wp_object_interest_matches_full (interest, \
            WP_TYPE_NODE, NULL, props, global_props)); \
    \
    g_clear_pointer (&interest, wp_object_interest_free); \
  } G_STMT_END

#define TEST_EXPECT_NO_MATCH_WP_PROPS(interest, props, global_props) \
  G_STMT_START { \
    g_autoptr (GError) error = NULL; \
    gboolean ret; \
    \
    g_assert_nonnull (interest); \
    \
    ret = wp_object_interest_validate (interest, &error); \
    g_assert_no_error (error); \
    g_assert_true (ret); \
    \
    g_assert_false (wp_object_interest_matches_full (interest, \
            WP_TYPE_NODE, NULL, props, global_props)); \
    \
    g_clear_pointer (&interest, wp_object_interest_free); \
  } G_STMT_END

#define TEST_EXPECT_VALIDATION_ERROR(interest) \
  G_STMT_START { \
    g_autoptr (GError) error = NULL; \
    gboolean ret; \
    \
    g_assert_nonnull (interest); \
    \
    ret = wp_object_interest_validate (interest, &error); \
    g_assert_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT); \
    g_assert_false (ret); \
    \
    g_clear_pointer (&interest, wp_object_interest_free); \
  } G_STMT_END

static void
test_object_interest_unconstrained (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpObjectInterest) i = NULL;

  i = wp_object_interest_new_type (TEST_TYPE_A);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new_type (WP_TYPE_PROXY);
  TEST_EXPECT_NO_MATCH (i);
}

static void
test_object_interest_constraint_equals (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpObjectInterest) i = NULL;

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "=s", "toast", NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "=s", "fail", NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int", "=i", -30, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int", "=i", 100, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "=u", 50, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "=u", 100, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int64",
      "=x", G_GINT64_CONSTANT (-0x1d636b02300a7aa7), NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int64",
      "=x", G_GINT64_CONSTANT (100), NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint64",
      "=t", G_GUINT64_CONSTANT (0x1d636b02300a7aa7), NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint64",
      "=t", G_GUINT64_CONSTANT (100), NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double",
      "=d", 3.1415926545897932384626433, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double", "=d", 3.14, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-float", "=d", 3.14, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-float", "=d", 1.0, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-boolean", "=b", TRUE, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-boolean", "=b", FALSE, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double", "=d", 3.1415926545897932384626433,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "=u", 50,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "=s", "toast",
      NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double",
      "=d", 3.1415926545897932384626433,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "=u", 50,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "=s", "FAIL",
      NULL);
  TEST_EXPECT_NO_MATCH (i);
}

static void
test_object_interest_constraint_list (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpObjectInterest) i = NULL;

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string",
      "c(sss)", "success", "toast", "test", NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string",
      "c(ss)", "not-a-toast", "fail", NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int", "c(iii)", -30, 20, -10, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int", "c(i)", 100, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "c(uu)", 100, 50, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "c(u)", 100, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int64", "c(xx)",
      G_GINT64_CONSTANT (100), G_GINT64_CONSTANT (-0x1d636b02300a7aa7), NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int64",
      "c(x)", G_GINT64_CONSTANT (100), NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint64",
      "c(t)", G_GUINT64_CONSTANT (0x1d636b02300a7aa7), NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint64",
      "c(t)", G_GUINT64_CONSTANT (100), NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double",
      "c(dd)", 2.0, 3.1415926545897932384626433, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double", "c(d)", 3.14, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-float", "c(dd)", 2.0, 3.14, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-float", "c(dd)", 1.0, 2.0, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double",
      "c(d)", 3.1415926545897932384626433,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "c(u)", 50,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "c(ss)", "random", "toast",
      NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double",
      "c(d)", 3.1415926545897932384626433,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "c(u)", 50,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "c(s)", "FAIL",
      NULL);
  TEST_EXPECT_NO_MATCH (i);
}

static void
test_object_interest_constraint_range (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpObjectInterest) i = NULL;

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int", "~(ii)", -40, 20, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int", "~(ii)", 10, 100, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "~(uu)", 40, 100, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "~(uu)", 100, 150, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int64", "~(xx)",
      G_GINT64_CONSTANT (-0x1d636b02300a7aaa),
      G_GINT64_CONSTANT (-0x1d636b02300a7aa0),
      NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int64", "~(xx)",
      G_GINT64_CONSTANT (0),
      G_GINT64_CONSTANT (100),
      NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint64", "~(tt)",
      G_GUINT64_CONSTANT (0x1d636b02300a7aa0),
      G_GUINT64_CONSTANT (0x1d636b02300a7aaa),
      NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint64", "~(tt)",
      G_GUINT64_CONSTANT (0),
      G_GUINT64_CONSTANT (100),
      NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double", "~(dd)", 2.0, 4.0, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double", "~(dd)", -1.0, 3.14, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-float", "~(dd)", 2.0, 4.0, NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-float", "~(dd)", -1.0, 3.13, NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double", "~(dd)", 0.0, 10.0,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "~(uu)", 0, 100,
      NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-double", "~(dd)", 10.0, 20.0,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-uint", "~(uu)", 0, 100,
      NULL);
  TEST_EXPECT_NO_MATCH (i);
}

static void
test_object_interest_constraint_matches (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpObjectInterest) i = NULL;

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "#s", "to*", NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "#s", "t*st", NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "#s", "*a?t", NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "#s", "egg*", NULL);
  TEST_EXPECT_NO_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "#s", "t?est", NULL);
  TEST_EXPECT_NO_MATCH (i);
}

static void
test_object_interest_constraint_present_absent (TestFixture * f,
    gconstpointer data)
{
  g_autoptr (WpObjectInterest) i = NULL;

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-int", "+", NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "toast", "+", NULL);
  TEST_EXPECT_NO_MATCH (i);


  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "toast", "-", NULL);
  TEST_EXPECT_MATCH (i);

  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "test-string", "-", NULL);
  TEST_EXPECT_NO_MATCH (i);
}

static void
test_object_interest_pw_props (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpProperties) global_props = NULL;
  g_autoptr (WpObjectInterest) i = NULL;

  props = wp_properties_new (
      "object.id", "10",
      "port.name", "test",
      "port.physical", "true",
      "audio.channel", "FR",
      "audio.volume", "0.8",
      "format.dsp", "32 bit float mono audio",
      NULL);

  global_props = wp_properties_new (
      "object.id", "10",
      "format.dsp", "32 bit float mono audio",
      NULL);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "~(ii)", 0, 100, NULL);
  TEST_EXPECT_MATCH_WP_PROPS (i, props, global_props);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "=i", 11, NULL);
  TEST_EXPECT_NO_MATCH_WP_PROPS (i, props, global_props);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "format.dsp", "#s", "*audio*", NULL);
  TEST_EXPECT_MATCH_WP_PROPS (i, props, global_props);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.physical", "=b", TRUE, NULL);
  TEST_EXPECT_MATCH_WP_PROPS (i, props, global_props);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "audio.channel", "c(sss)",
      "MONO", "FL", "FR", NULL);
  TEST_EXPECT_MATCH_WP_PROPS (i, props, global_props);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "audio.volume", "=d", 0.8, NULL);
  TEST_EXPECT_MATCH_WP_PROPS (i, props, global_props);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "audio.volume", "~(dd)", 0.0, 0.5,
      NULL);
  TEST_EXPECT_NO_MATCH_WP_PROPS (i, props, global_props);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "object.id", "=i", 10,
      NULL);
  TEST_EXPECT_MATCH_WP_PROPS (i, props, global_props);

  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "object.id", "+",
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "format.dsp", "+",
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "port.name", "-",
      WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "port.physical", "-",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.name", "+",
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.physical", "+",
      NULL);
  TEST_EXPECT_MATCH_WP_PROPS (i, props, global_props);
}

static void
test_object_interest_validate (TestFixture * f, gconstpointer data)
{
  g_autoptr (WpObjectInterest) i = NULL;

  /* invalid type */
  i = wp_object_interest_new (WP_TYPE_NODE, 32, "object.id", "+", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);

  /* non-WpPipewireObject type with pw property constraint */
  i = wp_object_interest_new (TEST_TYPE_A,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "+", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);

  /* bad verb; the varargs constructor would assert here */
  i = wp_object_interest_new_type (WP_TYPE_NODE);
  wp_object_interest_add_constraint (i, WP_CONSTRAINT_TYPE_PW_PROPERTY,
      "object.id", 0, g_variant_new_string ("10"));
  TEST_EXPECT_VALIDATION_ERROR (i);

  /* no subject; the varargs version would assert here */
  i = wp_object_interest_new_type (WP_TYPE_NODE);
  wp_object_interest_add_constraint (i, WP_CONSTRAINT_TYPE_PW_PROPERTY,
      NULL, WP_CONSTRAINT_VERB_EQUALS, g_variant_new_int32 (10));
  TEST_EXPECT_VALIDATION_ERROR (i);

  /* no value for verb that requires it */
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "=", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "~", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "c", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "#", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);

  /* value given for verb that doesn't require it */
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "+s", "10", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "-s", "10", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);

  /* tuple required */
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "ci", 10, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "~i", 10, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);

  /* invalid value type */
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "=y", (guchar) 10, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "=n", (gint16) 10, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "=q", (guint16) 10, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "c(bb)", TRUE, FALSE, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "~(ss)", "0", "20", NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "#i", 10, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);

  /* tuple with different types */
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "c(si)", "9", 10, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
  i = wp_object_interest_new (WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id", "~(iu)", -10, 20, NULL);
  TEST_EXPECT_VALIDATION_ERROR (i);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add ("/wp/object-interest/unconstrained",
      TestFixture, NULL,
      test_object_interest_setup,
      test_object_interest_unconstrained,
      test_object_interest_teardown);

  g_test_add ("/wp/object-interest/equals",
      TestFixture, NULL,
      test_object_interest_setup,
      test_object_interest_constraint_equals,
      test_object_interest_teardown);

  g_test_add ("/wp/object-interest/list",
      TestFixture, NULL,
      test_object_interest_setup,
      test_object_interest_constraint_list,
      test_object_interest_teardown);

  g_test_add ("/wp/object-interest/range",
      TestFixture, NULL,
      test_object_interest_setup,
      test_object_interest_constraint_range,
      test_object_interest_teardown);

  g_test_add ("/wp/object-interest/matches",
      TestFixture, NULL,
      test_object_interest_setup,
      test_object_interest_constraint_matches,
      test_object_interest_teardown);

  g_test_add ("/wp/object-interest/present-absent",
      TestFixture, NULL,
      test_object_interest_setup,
      test_object_interest_constraint_present_absent,
      test_object_interest_teardown);

  g_test_add ("/wp/object-interest/pw-props",
      TestFixture, NULL,
      test_object_interest_setup,
      test_object_interest_pw_props,
      test_object_interest_teardown);

  g_test_add ("/wp/object-interest/validate",
      TestFixture, NULL,
      test_object_interest_setup,
      test_object_interest_validate,
      test_object_interest_teardown);

  return g_test_run ();
}
