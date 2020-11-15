/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: object-interest
 * @title: Object Interest
 */

#define G_LOG_DOMAIN "wp-object-interest"

#include "object-interest.h"
#include "global-proxy.h"
#include "proxy-interfaces.h"
#include "debug.h"
#include "error.h"

#include <pipewire/pipewire.h>

struct constraint
{
  WpConstraintType type;
  WpConstraintVerb verb;
  gchar subject_type; /* a basic GVariantType as a single char */
  gchar *subject;
  GVariant *value;
};

struct _WpObjectInterest
{
  gboolean valid;
  GType gtype;
  struct pw_array constraints;
};

/**
 * WpObjectInterest:
 * An object interest is a helper that is used in #WpObjectManager to
 * declare interest in certain kinds of objects.
 *
 * An interest is defined by a #GType and a set of constraints on the object's
 * properties. An object "matches" the interest if it is of the specified
 * #GType (either the same type or a descendant of it) and all the constraints
 * are satisfied.
 */
G_DEFINE_BOXED_TYPE (WpObjectInterest, wp_object_interest,
                     wp_object_interest_copy, wp_object_interest_free)

/**
 * wp_object_interest_new:
 * @gtype: the type of the object to declare interest in
 * @...: a set of constraints, terminated with %NULL
 *
 * Creates a new interest that declares interest in objects of the specified
 * @gtype, with the constraints specified in the variable arguments.
 *
 * The variable arguments should be a list of constraints terminated with %NULL,
 * where each constraint consists of the following arguments:
 *  - a #WpConstraintType: the constraint type
 *  - a `const gchar *`: the subject name
 *  - a `const gchar *`: the format string
 *  - 0 or more arguments according to the format string
 *
 * The format string is interpreted as follows:
 *  - the first character is the constraint verb:
 *     - `=`: %WP_CONSTRAINT_VERB_EQUALS
 *     - `c`: %WP_CONSTRAINT_VERB_IN_LIST
 *     - `~`: %WP_CONSTRAINT_VERB_IN_RANGE
 *     - `#`: %WP_CONSTRAINT_VERB_MATCHES
 *     - `+`: %WP_CONSTRAINT_VERB_IS_PRESENT
 *     - `-`: %WP_CONSTRAINT_VERB_IS_ABSENT
 *  - the rest of the characters are interpreted as a #GVariant format string,
 *    as it would be used in g_variant_new()
 *
 * The rest of this function's arguments up to the start of the next constraint
 * depend on the #GVariant format part of the format string and are used to
 * construct a #GVariant for the constraint's value argument.
 *
 * For further reading on the constraint's arguments, see
 * wp_object_interest_add_constraint()
 *
 * For example, this interest matches objects that are descendands of #WpProxy
 * with a "bound-id" between 0 and 100 (inclusive), with a pipewire property
 * called "format.dsp" that contains the string "audio" somewhere in the value
 * and with a pipewire property "port.name" being present (with any value):
 * |[
 * interest = wp_object_interest_new (WP_TYPE_PROXY,
 *     WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "~(uu)", 0, 100,
 *     WP_CONSTRAINT_TYPE_PW_PROPERTY, "format.dsp", "#s", "*audio*",
 *     WP_CONSTRAINT_TYPE_PW_PROPERTY, "port.name", "+",
 *     NULL);
 * ]|
 *
 * Returns: (transfer full): the new object interest
 */
WpObjectInterest *
wp_object_interest_new (GType gtype, ...)
{
  WpObjectInterest *self;
  va_list args;
  va_start (args, gtype);
  self = wp_object_interest_new_valist (gtype, &args);
  va_end (args);
  return self;
}

/**
 * wp_object_interest_new_valist:
 * @gtype: the type of the object to declare interest in
 * @args: pointer to va_list containing the constraints
 *
 * va_list version of wp_object_interest_new()
 *
 * Returns: (transfer full): the new object interest
 */
WpObjectInterest *
wp_object_interest_new_valist (GType gtype, va_list *args)
{
  WpObjectInterest *self = wp_object_interest_new_type (gtype);
  WpConstraintType type;

  g_return_val_if_fail (self != NULL, NULL);

  for (type = va_arg (*args, WpConstraintType);
       type != WP_CONSTRAINT_TYPE_NONE;
       type = va_arg (*args, WpConstraintType))
  {
    const gchar *subject, *format;
    WpConstraintVerb verb = 0;
    GVariant *value = NULL;

    subject = va_arg (*args, const gchar *);
    g_return_val_if_fail (subject != NULL, NULL);

    format = va_arg (*args, const gchar *);
    g_return_val_if_fail (format != NULL, NULL);

    verb = format[0];
    if (verb != 0 && format[1] != '\0')
      value = g_variant_new_va (format + 1, NULL, args);

    wp_object_interest_add_constraint (self, type, subject, verb, value);
  }
  return self;
}

/**
 * wp_object_interest_new_type: (rename-to wp_object_interest_new)
 * @gtype: the type of the object to declare interest in
 *
 * Creates a new interest that declares interest in objects of the specified
 * @gtype, without any property constraints. To add property constraints,
 * you can call wp_object_interest_add_constraint() afterwards.
 *
 * Returns: (transfer full): the new object interest
 */
WpObjectInterest *
wp_object_interest_new_type (GType gtype)
{
  WpObjectInterest *self = g_slice_new0 (WpObjectInterest);
  g_return_val_if_fail (self != NULL, NULL);
  self->gtype = gtype;
  pw_array_init (&self->constraints, sizeof (struct constraint));
  return self;
}

/**
 * wp_object_interest_add_constraint:
 * @self: the object interest
 * @type: the constraint type
 * @subject: the subject that the constraint applies to
 * @verb: the operation that is performed to check the constraint
 * @value: (transfer floating)(nullable): the value to check for
 *
 * Adds a constraint to this interest. Constraints consist of a @type,
 * a @subject, a @verb and, depending on the @verb, a @value.
 *
 * Constraints are almost like a spoken language sentence that declare a
 * condition that must be true in order to consider that an object can match
 * this interest. For instance, a constraint can be "pipewire property
 * 'object.id' equals 10". This would be translated to:
 * |[
 * wp_object_interest_add_constraint (i,
 *    WP_CONSTRAINT_TYPE_PW_PROPERTY, "object.id",
 *    WP_CONSTRAINT_VERB_EQUALS, g_variant_new_int (10));
 * ]|
 *
 * Some verbs require a @value and some others do not. For those that do,
 * the @value must be of a specific type:
 *  - %WP_CONSTRAINT_VERB_EQUALS: @value can be a string, a (u)int32,
 *    a (u)int64, a double or a boolean. The @subject value must equal this
 *    value for the constraint to be satisfied
 *  - %WP_CONSTRAINT_VERB_IN_LIST: @value must be a tuple that contains any
 *    number of items of the same type; the items can be string, (u)int32,
 *    (u)int64 or double. These items make a list that the @subject's value
 *    will be checked against. If any of the items equals the @subject value,
 *    the constraint is satisfied
 *  - %WP_CONSTRAINT_VERB_IN_RANGE: @value must be a tuple that contains exactly
 *    2 numbers of the same type ((u)int32, (u)int64 or double), meaning the
 *    minimum and maximum (inclusive) of the range. If the @subject value is a
 *    number within this range, the constraint is satisfied
 *  - %WP_CONSTRAINT_VERB_MATCHES: @value must be a string that defines a
 *    pattern usable with #GPatternSpec. If the @subject value matches this
 *    pattern, the constraint is satisfied
 *
 * In case the type of the @subject value is not the same type as the one
 * requested by the type of the @value, the @subject value is converted.
 * For #GObject properties, this conversion is done using g_value_transform(),
 * so limitations of this function apply. In the case of PipeWire properties,
 * which are *always* strings, conversion is done as follows:
 *  - to boolean: `"true"` or `"1"` means %TRUE, `"false"` or `"0"` means %FALSE
 *  - to int / uint / int64 / uint64: One of the `strtol()` family of functions
 *    is used to convert, using base 10
 *  - to double: `strtod()` is used
 *
 * This method does not fail if invalid arguments are given. However,
 * wp_object_interest_validate() should be called after adding all the
 * constraints on an interest in order to catch errors.
 */
void
wp_object_interest_add_constraint (WpObjectInterest * self,
    WpConstraintType type, const gchar * subject,
    WpConstraintVerb verb, GVariant * value)
{
  struct constraint *c;

  g_return_if_fail (self != NULL);

  c = pw_array_add (&self->constraints, sizeof (struct constraint));
  g_return_if_fail (c != NULL);
  c->type = type;
  c->verb = verb;
  /* subject_type is filled in by _validate() */
  c->subject_type = '\0';
  c->subject = g_strdup (subject);
  c->value = value ? g_variant_ref_sink (value) : NULL;

  /* mark as invalid to force validation */
  self->valid = FALSE;
}

/**
 * wp_object_interest_copy:
 * @self: the object interest to copy
 *
 * Returns: (transfer full): a deep copy of @self
 */
WpObjectInterest *
wp_object_interest_copy (WpObjectInterest * self)
{
  WpObjectInterest *copy;
  struct constraint *c, *cc;

  g_return_val_if_fail (self != NULL, NULL);

  copy = wp_object_interest_new_type (self->gtype);
  g_return_val_if_fail (copy != NULL, NULL);

  pw_array_ensure_size (&copy->constraints, self->constraints.size);
  pw_array_for_each (c, &self->constraints) {
    cc = pw_array_add (&self->constraints, sizeof (struct constraint));
    g_return_val_if_fail (cc != NULL, NULL);
    cc->type = c->type;
    cc->verb = c->verb;
    cc->subject_type = c->subject_type;
    cc->subject = g_strdup (c->subject);
    cc->value = c->value ? g_variant_ref (c->value) : NULL;
  }
  copy->valid = self->valid;

  return copy;
}

/**
 * wp_object_interest_free:
 * @self: (transfer full): the object interest to free
 *
 * Releases @self and all the memory associated with it
 */
void
wp_object_interest_free (WpObjectInterest * self)
{
  struct constraint *c;

  g_return_if_fail (self != NULL);

  pw_array_for_each (c, &self->constraints) {
    g_clear_pointer (&c->subject, g_free);
    g_clear_pointer (&c->value, g_variant_unref);
  }
  pw_array_clear (&self->constraints);
  g_slice_free (WpObjectInterest, self);
}

/**
 * wp_object_interest_validate:
 * @self: the object interest to validate
 * @error: (out) (optional): the error, in case validation failed
 *
 * Validates the interest, ensuring that the interest #GType is a valid
 * object and that all the constraints have been expressed properly.
 *
 * Returns: %TRUE if the interest is valid and can be used in a match,
 *   %FALSE otherwise
 */
gboolean
wp_object_interest_validate (WpObjectInterest * self, GError ** error)
{
  struct constraint *c;
  gboolean is_pwobj;
  gboolean is_global;

  g_return_val_if_fail (self != NULL, FALSE);

  /* if already validated, we are done */
  if (self->valid)
    return TRUE;

  if (!G_TYPE_IS_OBJECT (self->gtype)) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
        "type '%s' is not a GObject", g_type_name (self->gtype));
    return FALSE;
  }

  is_pwobj = g_type_is_a (self->gtype, WP_TYPE_PIPEWIRE_OBJECT);
  is_global = g_type_is_a (self->gtype, WP_TYPE_GLOBAL_PROXY);

  pw_array_for_each (c, &self->constraints) {
    const GVariantType *value_type = NULL;

    if (c->type <= WP_CONSTRAINT_TYPE_NONE ||
        c->type > WP_CONSTRAINT_TYPE_G_PROPERTY) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
          "invalid constraint type %d", c->type);
      return FALSE;
    }

    if (!is_pwobj && c->type == WP_CONSTRAINT_TYPE_PW_PROPERTY) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
          "constraint type %d cannot apply to non-WpPipewireObject type '%s'",
          c->type, g_type_name (self->gtype));
      return FALSE;
    }

    if (!is_global && c->type == WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
          "constraint type %d cannot apply to non-WpGlobalProxy type '%s'",
          c->type, g_type_name (self->gtype));
      return FALSE;
    }

    if (!c->subject) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
          "constraint subject cannot be NULL");
      return FALSE;
    }

    switch (c->verb) {
      case WP_CONSTRAINT_VERB_EQUALS:
      case WP_CONSTRAINT_VERB_IN_LIST:
      case WP_CONSTRAINT_VERB_IN_RANGE:
      case WP_CONSTRAINT_VERB_MATCHES:
        if (!c->value) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "verb %d (%c) requires a value", c->verb, (gchar) c->verb);
          return FALSE;
        }
        value_type = g_variant_get_type (c->value);
        break;

      case WP_CONSTRAINT_VERB_IS_PRESENT:
      case WP_CONSTRAINT_VERB_IS_ABSENT:
        if (c->value) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "verb %d (%c) should not have a value", c->verb, (gchar) c->verb);
          return FALSE;
        }
        break;

      default:
        g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
            "invalid constraint verb %d (%c)", c->verb, (gchar) c->verb);
        return FALSE;
    }

    switch (c->verb) {
      case WP_CONSTRAINT_VERB_EQUALS:
        if (!g_variant_type_equal (value_type, G_VARIANT_TYPE_STRING) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_BOOLEAN) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_INT32) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_UINT32) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_INT64) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_UINT64) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_DOUBLE)) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "WP_CONSTRAINT_VERB_EQUALS requires a basic GVariant type"
              " (actual type was '%s')", g_variant_get_type_string (c->value));
          return FALSE;
        }

        break;
      case WP_CONSTRAINT_VERB_IN_LIST: {
        const GVariantType *tuple_type;

        if (!g_variant_type_is_definite (value_type) ||
            !g_variant_type_is_tuple (value_type)) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "WP_CONSTRAINT_VERB_IN_LIST requires a tuple GVariant type"
              " (actual type was '%s')", g_variant_get_type_string (c->value));
          return FALSE;
        }

        for (tuple_type = value_type = g_variant_type_first (value_type);
            tuple_type != NULL;
            tuple_type = g_variant_type_next (tuple_type)) {
          if (!g_variant_type_equal (tuple_type, value_type)) {
            g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
                "tuple must contain children of the same type"
                " (mismatching type was '%s' at '%.*s')",
                g_variant_get_type_string (c->value),
                (int) g_variant_type_get_string_length (tuple_type),
                g_variant_type_peek_string (tuple_type));
            return FALSE;
          }
        }

        if (!g_variant_type_equal (value_type, G_VARIANT_TYPE_STRING) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_INT32) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_UINT32) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_INT64) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_UINT64) &&
            !g_variant_type_equal (value_type, G_VARIANT_TYPE_DOUBLE)) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "list tuple must contain string, (u)int32, (u)int64 or double"
              " (mismatching type was '%s' at '%.*s')",
              g_variant_get_type_string (c->value),
              (int) g_variant_type_get_string_length (value_type),
              g_variant_type_peek_string (value_type));
          return FALSE;
        }

        break;
      }
      case WP_CONSTRAINT_VERB_IN_RANGE: {
        const GVariantType *tuple_type;

        if (!g_variant_type_is_definite (value_type) ||
            !g_variant_type_is_tuple (value_type)) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "range requires a tuple GVariant type (actual type was '%s')",
              g_variant_get_type_string (c->value));
          return FALSE;
        }

        tuple_type = value_type = g_variant_type_first (value_type);
        if (!tuple_type) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "range requires a non-empty tuple (actual type was '%s')",
              g_variant_get_type_string (c->value));
          return FALSE;
        }

        if (!g_variant_type_equal (tuple_type, G_VARIANT_TYPE_INT32) &&
            !g_variant_type_equal (tuple_type, G_VARIANT_TYPE_UINT32) &&
            !g_variant_type_equal (tuple_type, G_VARIANT_TYPE_INT64) &&
            !g_variant_type_equal (tuple_type, G_VARIANT_TYPE_UINT64) &&
            !g_variant_type_equal (tuple_type, G_VARIANT_TYPE_DOUBLE)) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "range tuple must contain (u)int32, (u)int64 or double"
              " (mismatching type was '%s' at '%.*s')",
              g_variant_get_type_string (c->value),
              (int) g_variant_type_get_string_length (tuple_type),
              g_variant_type_peek_string (tuple_type));
          return FALSE;
        }

        tuple_type = g_variant_type_next (tuple_type);
        if (!tuple_type || !g_variant_type_equal (tuple_type, value_type)) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "range tuple must contain 2 children of the same type"
              " (mismatching type was '%s' at '%.*s')",
              g_variant_get_type_string (c->value),
              (int) g_variant_type_get_string_length (tuple_type),
              g_variant_type_peek_string (tuple_type));
          return FALSE;
        }

        tuple_type = g_variant_type_next (tuple_type);
        if (tuple_type) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "range tuple must contain exactly 2 children, not more"
              " (mismatching type was '%s')",
              g_variant_get_type_string (c->value));
          return FALSE;
        }

        break;
      }
      case WP_CONSTRAINT_VERB_MATCHES:
        if (!g_variant_type_equal (value_type, G_VARIANT_TYPE_STRING)) {
          g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVARIANT,
              "WP_CONSTRAINT_VERB_MATCHES requires a string GVariant"
              " (actual type was '%s')", g_variant_get_type_string (c->value));
          return FALSE;
        }

        break;
      case WP_CONSTRAINT_VERB_IS_PRESENT:
      case WP_CONSTRAINT_VERB_IS_ABSENT:
        break;
      default:
        g_return_val_if_reached (FALSE);
    }

    /* cache the type that the property must have */
    if (value_type)
      c->subject_type = *g_variant_type_peek_string (value_type);
  }

  return (self->valid = TRUE);
}

G_GNUC_CONST static GType
subject_type_to_gtype (gchar type)
{
  switch (type) {
    case 'b': return G_TYPE_BOOLEAN;
    case 'i': return G_TYPE_INT;
    case 'u': return G_TYPE_UINT;
    case 'x': return G_TYPE_INT64;
    case 't': return G_TYPE_UINT64;
    case 'd': return G_TYPE_DOUBLE;
    case 's': return G_TYPE_STRING;
    default: g_return_val_if_reached (G_TYPE_INVALID);
  }
}

static inline gboolean
property_string_to_gvalue (gchar subj_type, const gchar * str, GValue * val)
{
  g_value_init (val, subject_type_to_gtype (subj_type));

  switch (subj_type) {
    case 'b':
      if (!strcmp (str, "true") || !strcmp (str, "1"))
        g_value_set_boolean (val, TRUE);
      else if (!strcmp (str, "false") || !strcmp (str, "0"))
        g_value_set_boolean (val, FALSE);
      else {
        wp_trace ("failed to convert '%s' to boolean", str);
        return FALSE;
      }
      break;
    case 's':
      g_value_set_static_string (val, str);
      break;

#define CASE_NUMBER(l, T, convert) \
    case l: { \
      g##T number; \
      errno = 0; \
      number = convert; \
      if (errno != 0) { \
        wp_trace ("failed to convert '%s' to " #T, str); \
        return FALSE; \
      } \
      g_value_set_##T (val, number); \
      break; \
    }
    CASE_NUMBER ('i', int, strtol (str, NULL, 10))
    CASE_NUMBER ('u', uint, strtoul (str, NULL, 10))
    CASE_NUMBER ('x', int64, strtoll (str, NULL, 10))
    CASE_NUMBER ('t', uint64, strtoull (str, NULL, 10))
    CASE_NUMBER ('d', double, strtod (str, NULL))
#undef CASE_NUMBER
    default:
      g_return_val_if_reached (FALSE);
  }
  return TRUE;
}

static inline gboolean
constraint_verb_equals (gchar subj_type, const GValue * subj_val,
    GVariant * check_val)
{
  switch (subj_type) {
    case 'd': {
      gdouble a = g_value_get_double (subj_val);
      gdouble b = g_variant_get_double (check_val);
      return G_APPROX_VALUE (a, b, FLT_EPSILON);
    }
    case 's':
      return !g_strcmp0 (g_value_get_string (subj_val),
                         g_variant_get_string (check_val, NULL));
#define CASE_BASIC(l, T, R) \
    case l: \
      return (g_value_get_##T (subj_val) == g_variant_get_##R (check_val));
    CASE_BASIC ('b', boolean, boolean)
    CASE_BASIC ('i', int, int32)
    CASE_BASIC ('u', uint, uint32)
    CASE_BASIC ('x', int64, int64)
    CASE_BASIC ('t', uint64, uint64)
#undef CASE_BASIC
    default:
      g_return_val_if_reached (FALSE);
  }
}

static inline gboolean
constraint_verb_matches (gchar subj_type, const GValue * subj_val,
    GVariant * check_val)
{
  switch (subj_type) {
    case 's':
      return g_pattern_match_simple (g_variant_get_string (check_val, NULL),
                                     g_value_get_string (subj_val));
    default:
      g_return_val_if_reached (FALSE);
  }
  return TRUE;
}

static inline gboolean
constraint_verb_in_list (gchar subj_type, const GValue * subj_val,
    GVariant * check_val)
{
  GVariantIter iter;
  g_autoptr (GVariant) child = NULL;

  g_variant_iter_init (&iter, check_val);
  while ((child = g_variant_iter_next_value (&iter))) {
    if (constraint_verb_equals (subj_type, subj_val, child))
      return TRUE;
    g_variant_unref (child);
  }
  return FALSE;
}

static inline gboolean
constraint_verb_in_range (gchar subj_type, const GValue * subj_val,
    GVariant * check_val)
{
  switch (subj_type) {
#define CASE_RANGE(l, t, T) \
    case l: { \
      g##T val, min, max; \
      g_variant_get (check_val, "("#t#t")", &min, &max); \
      val = g_value_get_##T (subj_val); \
      if (val < min || val > max) \
        return FALSE; \
      break; \
    }
    CASE_RANGE('i', i, int)
    CASE_RANGE('u', u, uint)
    CASE_RANGE('x', x, int64)
    CASE_RANGE('t', t, uint64)
    CASE_RANGE('d', d, double)
#undef CASE_RANGE
    default:
      g_return_val_if_reached (FALSE);
  }
  return TRUE;
}

/**
 * wp_object_interest_matches:
 * @self: the object interest
 * @object: the target object to check for a match
 *
 * Checks if the specified @object matches the type and all the constraints
 * that are described in @self
 *
 * Equivalent to `wp_object_interest_matches_full (self,
 * G_OBJECT_TYPE (object), object, NULL, NULL)`
 *
 * Returns: %TRUE if the object matches, %FALSE otherwise
 */
gboolean
wp_object_interest_matches (WpObjectInterest * self, gpointer object)
{
  return wp_object_interest_matches_full (self, G_OBJECT_TYPE (object),
      object, NULL, NULL);
}

/**
 * wp_object_interest_matches_full:
 * @self: the object interest
 * @object_type: the type to be checked against the interest's type
 * @object: (type GObject)(transfer none)(nullable): the object to be used for
 *   checking constraints of type %WP_CONSTRAINT_TYPE_G_PROPERTY
 * @pw_props: (transfer none)(nullable): the properties to be used for
 *   checking constraints of type %WP_CONSTRAINT_TYPE_PW_PROPERTY
 * @pw_global_props: (transfer none)(nullable): the properties to be used for
 *   checking constraints of type %WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY
 *
 * A low-level version of wp_object_interest_matches(). In this version,
 * the object's type is directly given in @object_type and is not inferred
 * from the @object. @object is only used to check for constraints against
 * #GObject properties.
 *
 * @pw_props and @pw_global_props are used to check constraints against
 * PipeWire object properties and global properties, respectively.
 *
 * @object, @pw_props and @pw_global_props may be %NULL, but in case there
 * are any constraints that require them, the match will fail.
 * As a special case, if @object is not %NULL and is a subclass of #WpProxy,
 * then @pw_props and @pw_global_props, if required, will be internally
 * retrieved from @object by calling wp_pipewire_object_get_properties() and
 * wp_global_proxy_get_global_properties() respectively.
 *
 * Returns: %TRUE if the the type matches this interest and the properties
 *   match the constraints, %FALSE otherwise
 */
gboolean
wp_object_interest_matches_full (WpObjectInterest * self,
    GType object_type, gpointer object, WpProperties * pw_props,
    WpProperties * pw_global_props)
{
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpProperties) global_props = NULL;
  g_autoptr (GError) error = NULL;
  struct constraint *c;

  g_return_val_if_fail (self != NULL, FALSE);

  if (G_UNLIKELY (!wp_object_interest_validate (self, &error))) {
    wp_critical_boxed (WP_TYPE_OBJECT_INTEREST, self, "validation failed: %s",
        error->message);
    return FALSE;
  }

  /* check if the GType matches */
  if (!g_type_is_a (object_type, self->gtype))
    return FALSE;

  /* prepare for constraint lookups on proxy properties */
  if (object) {
    if (!pw_global_props && WP_IS_GLOBAL_PROXY (object)) {
      WpGlobalProxy *pwg = (WpGlobalProxy *) object;
      pw_global_props = global_props =
          wp_global_proxy_get_global_properties (pwg);
    }

    if (!pw_props && WP_IS_PIPEWIRE_OBJECT (object)) {
      WpObject *oo = (WpObject *) object;
      WpPipewireObject *pwo = (WpPipewireObject *) object;

      if (wp_object_get_active_features (oo) & WP_PIPEWIRE_OBJECT_FEATURE_INFO)
        pw_props = props = wp_pipewire_object_get_properties (pwo);
    }
  }

  /* check all constraints; if any of them fails at any point, fail the match */
  pw_array_for_each (c, &self->constraints) {
    WpProperties *lookup_props = pw_global_props;
    g_auto (GValue) value = G_VALUE_INIT;
    gboolean exists = FALSE;

    /* collect, check & convert the subject property */
    switch (c->type) {
      case WP_CONSTRAINT_TYPE_PW_PROPERTY:
        lookup_props = pw_props;
        /* G_GNUC_FALLTHROUGH */;

      case WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY: {
        const gchar *lookup_str = NULL;

        if (lookup_props)
          exists = !!(lookup_str = wp_properties_get (lookup_props, c->subject));

        if (exists && c->subject_type)
          property_string_to_gvalue (c->subject_type, lookup_str, &value);
        break;
      }
      case WP_CONSTRAINT_TYPE_G_PROPERTY: {
        GType value_type;
        GParamSpec *pspec = NULL;

        if (object)
          exists = !!(pspec = g_object_class_find_property (
              G_OBJECT_GET_CLASS (object), c->subject));

        if (exists && c->subject_type) {
          g_value_init (&value, pspec->value_type);
          g_object_get_property (object, c->subject, &value);
          value_type = G_VALUE_TYPE (&value);

          /* transform if not compatible */
          if (value_type != subject_type_to_gtype (c->subject_type)) {
            if (g_value_type_transformable (value_type,
                    subject_type_to_gtype (c->subject_type))) {
              g_auto (GValue) orig = G_VALUE_INIT;
              g_value_init (&orig, value_type);
              g_value_copy (&value, &orig);
              g_value_unset (&value);
              g_value_init (&value, subject_type_to_gtype (c->subject_type));
              g_value_transform (&orig, &value);
            }
            else
              return FALSE;
          }
        }

        break;
      }
      default:
        g_return_val_if_reached (FALSE);
    }

    /* match the subject to the constraint's value,
       according to the operation defined by the verb */
    switch (c->verb) {
      case WP_CONSTRAINT_VERB_EQUALS:
        if (!exists ||
            !constraint_verb_equals (c->subject_type, &value, c->value))
          return FALSE;
        break;
      case WP_CONSTRAINT_VERB_MATCHES:
        if (!exists ||
            !constraint_verb_matches (c->subject_type, &value, c->value))
          return FALSE;
        break;
      case WP_CONSTRAINT_VERB_IN_LIST:
        if (!exists ||
            !constraint_verb_in_list (c->subject_type, &value, c->value))
          return FALSE;
        break;
      case WP_CONSTRAINT_VERB_IN_RANGE:
        if (!exists ||
            !constraint_verb_in_range (c->subject_type, &value, c->value))
          return FALSE;
        break;
      case WP_CONSTRAINT_VERB_IS_PRESENT:
        if (!exists)
          return FALSE;
        break;
      case WP_CONSTRAINT_VERB_IS_ABSENT:
        if (exists)
          return FALSE;
        break;
      default:
        g_return_val_if_reached (FALSE);
    }
  }
  return TRUE;
}
