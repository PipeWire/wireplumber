/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "lua.h"
#include <wplua/wplua.h>
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

typedef struct _TestObject TestObject;
struct _TestObject
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

typedef struct _TestObjectClass TestObjectClass;
struct _TestObjectClass
{
  GObjectClass parent_class;

  void (*change) (TestObject * self, const gchar * str, gint integer);
};

G_DEFINE_TYPE (TestObject, test_object, G_TYPE_OBJECT)

#define TEST_TYPE_OBJECT (test_object_get_type ())
_GLIB_DEFINE_AUTOPTR_CHAINUP (TestObject, GObject)

static inline TestObject * TEST_OBJECT (gpointer ptr) {
  return G_TYPE_CHECK_INSTANCE_CAST (ptr, TEST_TYPE_OBJECT, TestObject);
}

static void
test_object_init (TestObject * self)
{
}

static void
test_object_finalize (GObject * object)
{
  TestObject *self = TEST_OBJECT (object);
  g_free (self->test_string);
  G_OBJECT_CLASS (test_object_parent_class)->finalize (object);
}

static void
test_object_get_property (GObject * object, guint id, GValue * value,
    GParamSpec * pspec)
{
  TestObject *self = TEST_OBJECT (object);

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
test_object_set_property (GObject * object, guint id, const GValue * value,
    GParamSpec * pspec)
{
  TestObject *self = TEST_OBJECT (object);

  switch (id) {
    case PROP_TEST_STRING:
      g_free (self->test_string);
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
test_object_change (TestObject * self, const gchar * str, gint integer)
{
  g_free (self->test_string);
  self->test_string = g_strdup_printf ("changed: %s", str);
  g_object_notify (G_OBJECT (self), "test-string");
  self->test_int = integer;
  g_object_notify (G_OBJECT (self), "test-int");

  gint ret = 0;
  g_signal_emit_by_name (self, "acquire", &ret);
  self->test_int64 = ret;
  g_object_notify (G_OBJECT (self), "test-int64");
}

static void
test_object_class_init (TestObjectClass * klass)
{
  GObjectClass *obj_class = (GObjectClass *) klass;

  obj_class->finalize = test_object_finalize;
  obj_class->get_property = test_object_get_property;
  obj_class->set_property = test_object_set_property;

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

  g_signal_new ("change", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (TestObjectClass, change), NULL, NULL, NULL,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);
  klass->change = test_object_change;

  g_signal_new ("acquire", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_INT, 0);
}

static void
test_object_toggle (TestObject * self)
{
  self->test_boolean = !self->test_boolean;
  g_object_notify (G_OBJECT (self), "test-boolean");
}

static int
l_test_object_toggle (lua_State * L)
{
  TestObject * self = wplua_checkobject (L, 1, TEST_TYPE_OBJECT);
  test_object_toggle (self);
  return 0;
}

static const luaL_Reg l_test_object_methods[] = {
  { "toggle", l_test_object_toggle },
  { NULL, NULL }
};

static int
l_test_object_new (lua_State * L)
{
  wplua_pushobject (L, g_object_new (TEST_TYPE_OBJECT, NULL));
  return 1;
}

static void
test_wplua_basic ()
{
  lua_State *L = wplua_new ();
  wplua_free (L);
}

static void
test_wplua_construct ()
{
  g_autoptr (GObject) obj = NULL;
  g_autoptr (GError) error = NULL;
  lua_State *L = wplua_new ();

  wplua_register_type_methods(L, TEST_TYPE_OBJECT,
      l_test_object_new, l_test_object_methods);

  const gchar code[] =
    "o = TestObject.new()\n"
    "assert (type(o) == 'userdata')\n";
  wplua_load_buffer (L, code, sizeof (code) - 1, &error);
  g_assert_no_error (error);

  g_assert_cmpint (lua_getglobal (L, "o"), ==, LUA_TUSERDATA);
  g_assert_true (wplua_checkobject (L, -1, TEST_TYPE_OBJECT));
  g_assert_nonnull ((obj = wplua_toobject (L, -1)));
  g_object_ref (obj);
  g_assert_cmpint (obj->ref_count, ==, 2);

  wplua_free (L);
  g_assert_cmpint (obj->ref_count, ==, 1);
}

static void
test_wplua_properties ()
{
  TestObject *obj = NULL;
  g_autoptr (GError) error = NULL;
  lua_State *L = wplua_new ();

  wplua_register_type_methods(L, TEST_TYPE_OBJECT,
      l_test_object_new, l_test_object_methods);

  const gchar code[] =
    "o = TestObject.new()\n"
    "o['test-string'] = 'string from lua'\n"
    "o['test-int'] = -15\n"
    "o['test-uint'] = 1123456789\n"
    "o['test-int64'] = -5123456789\n"
    "o['test-uint64'] = 15123456789\n"
    "o['test-float'] = 3.1415\n"
    "o['test-double'] = 0.123456789\n"
    "o['test-boolean'] = true\n";
  wplua_load_buffer (L, code, sizeof (code) - 1, &error);
  g_assert_no_error (error);

  g_assert_cmpint (lua_getglobal (L, "o"), ==, LUA_TUSERDATA);
  g_assert_true (wplua_checkobject (L, -1, TEST_TYPE_OBJECT));
  g_assert_nonnull ((obj = wplua_toobject (L, -1)));

  g_assert_cmpstr (obj->test_string, ==, "string from lua");
  g_assert_cmpint (obj->test_int, ==, -15);
  g_assert_cmpuint (obj->test_uint, ==, 1123456789);
  g_assert_cmpint (obj->test_int64, ==, -5123456789);
  g_assert_cmpuint (obj->test_uint64, ==, 15123456789);
  g_assert_cmpfloat_with_epsilon (obj->test_float, 3.1415, 0.00001);
  g_assert_cmpfloat_with_epsilon (obj->test_double, 0.123456789, 0.0000000001);
  g_assert_true (obj->test_boolean);

  const gchar code2[] =
    "assert (o['test-string'] == 'string from lua')\n"
    "assert (o['test-int'] == -15)\n"
    "assert (o['test-uint'] == 1123456789)\n"
    "assert (o['test-int64'] == -5123456789)\n"
    "assert (o['test-uint64'] == 15123456789)\n"
    "assert (math.abs (o['test-float'] - 3.1415) < 0.00001)\n"
    "assert (math.abs (o['test-double'] - 0.123456789) < 0.0000000001)\n"
    "assert (o['test-boolean'] == true)\n";
  wplua_load_buffer (L, code2, sizeof (code2) - 1, &error);
  g_assert_no_error (error);

  wplua_free (L);
}

static void
test_wplua_closure ()
{
  GClosure *closure;
  g_autoptr (GError) error = NULL;
  lua_State *L = wplua_new ();

  lua_pushstring (L, "some string");
  lua_setglobal (L, "expected_str");

  const gchar code[] =
    "f_was_called = false\n"
    "function f(s)\n"
    "  assert(s == expected_str)\n"
    "  f_was_called = true\n"
    "end\n";
  wplua_load_buffer (L, code, sizeof (code) - 1, &error);
  g_assert_no_error (error);

  lua_getglobal (L, "f");
  closure = wplua_function_to_closure (L, -1);
  g_assert_nonnull (closure);
  g_closure_ref (closure);
  g_closure_sink (closure);
  lua_pop (L, 1);

  {
    GValue s = G_VALUE_INIT;
    g_value_init (&s, G_TYPE_STRING);
    g_value_set_static_string (&s, "some string");
    g_closure_invoke (closure, NULL, 1, &s, NULL);
  }

  lua_getglobal (L, "f_was_called");
  g_assert_true (lua_isboolean (L, -1));
  g_assert_true (lua_toboolean (L, -1));

  wplua_free (L);

  g_assert_true (closure->is_invalid);
  g_closure_unref (closure);
}

static void
test_wplua_signals ()
{
  g_autoptr (GError) error = NULL;
  lua_State *L = wplua_new ();

  wplua_register_type_methods(L, TEST_TYPE_OBJECT,
      l_test_object_new, l_test_object_methods);

  const gchar code[] =
    "o = TestObject.new()\n"
    "\n"
    "o:connect('acquire', function (obj)\n"
    "    assert(obj == o)\n"
    "    return 42\n"
    "  end)\n"
    "\n"
    "o:connect('notify::test-string', function (obj, pspec)\n"
    "    assert(pspec == 'test-string')\n"
    "    assert(obj[pspec] == 'changed: by Lua')\n"
    "  end)\n"
    "\n"
    "o:call('change', 'by Lua', 55)\n"
    "\n"
    "assert(o['test-string'] == 'changed: by Lua')\n"
    "assert(o['test-int'] == 55)\n"
    "assert(o['test-int64'] == 42)\n"
    "\n"
    "o['test-boolean'] = true\n"
    "o:toggle()\n"
    "assert(o['test-boolean'] == false)\n";
  wplua_load_buffer (L, code, sizeof (code) - 1, &error);
  g_assert_no_error (error);
  wplua_free (L);
}

static void
test_wplua_sandbox ()
{
  g_autoptr (GError) error = NULL;
  lua_State *L = wplua_new ();

  wplua_register_type_methods(L, TEST_TYPE_OBJECT,
      l_test_object_new, l_test_object_methods);

  const gchar code[] =
    "SANDBOX_EXPORT = {\n"
    "  Test = TestObject.new,\n"
    "  Table = { test = 'foobar' }\n"
    "}\n";
  wplua_load_buffer (L, code, sizeof (code) - 1, &error);
  g_assert_no_error (error);

  wplua_enable_sandbox (L);

  const gchar code2[] =
    "o = TestObject.new()\n";
  wplua_load_buffer (L, code2, sizeof (code2) - 1, &error);
  g_debug ("expected error: %s", error ? error->message : "null");
  g_assert_error (error, WP_DOMAIN_LUA, WP_LUA_ERROR_RUNTIME);
  g_clear_error (&error);

  const gchar code3[] =
    "o = Test()\n";
  wplua_load_buffer (L, code3, sizeof (code3) - 1, &error);
  g_assert_no_error (error);

  const gchar code4[] =
    "assert(string.len(Table.test) == 6)\n";
  wplua_load_buffer (L, code4, sizeof (code4) - 1, &error);
  g_assert_no_error (error);

  const gchar code5[] =
    "o:call('change', 'by Lua', 55)\n";
  wplua_load_buffer (L, code5, sizeof (code5) - 1, &error);
  g_debug ("expected error: %s", error ? error->message : "null");
  g_assert_error (error, WP_DOMAIN_LUA, WP_LUA_ERROR_RUNTIME);
  g_clear_error (&error);

  const gchar code6[] =
    "string.test = 'hello world'\n";
  wplua_load_buffer (L, code6, sizeof (code6) - 1, &error);
  g_debug ("expected error: %s", error ? error->message : "null");
  g_assert_error (error, WP_DOMAIN_LUA, WP_LUA_ERROR_RUNTIME);
  g_clear_error (&error);

  const gchar code7[] =
    "Table.test = 'hello world'\n";
  wplua_load_buffer (L, code7, sizeof (code7) - 1, &error);
  g_debug ("expected error: %s", error ? error->message : "null");
  g_assert_error (error, WP_DOMAIN_LUA, WP_LUA_ERROR_RUNTIME);
  g_clear_error (&error);

  wplua_free (L);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add_func ("/wplua/basic", test_wplua_basic);
  g_test_add_func ("/wplua/construct", test_wplua_construct);
  g_test_add_func ("/wplua/properties", test_wplua_properties);
  g_test_add_func ("/wplua/closure", test_wplua_closure);
  g_test_add_func ("/wplua/signals", test_wplua_signals);
  g_test_add_func ("/wplua/sandbox", test_wplua_sandbox);

  return g_test_run ();
}
