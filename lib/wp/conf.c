/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"
#include "conf.h"
#include "log.h"
#include "object-interest.h"
#include "private/registry.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-conf")

#define OVERRIDE_SECTION_PREFIX "override."

/*! \defgroup wpconf WpConf */
/*!
 * \struct WpConf
 *
 * WpConf allows accessing the different sections of the wireplumber
 * configuration.
 */

struct _WpConf
{
  GObject parent;

  /* Props */
  GWeakRef core;

  GHashTable *sections;
};

enum {
  PROP_0,
  PROP_CORE,
};

G_DEFINE_TYPE (WpConf, wp_conf, G_TYPE_OBJECT)

static void
wp_conf_init (WpConf * self)
{
  g_weak_ref_init (&self->core, NULL);

  self->sections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) wp_spa_json_unref);
}

static void
wp_conf_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpConf *self = WP_CONF (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_conf_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpConf *self = WP_CONF (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_conf_finalize (GObject * object)
{
  WpConf *self = WP_CONF (object);

  g_clear_pointer (&self->sections, g_hash_table_unref);
  g_weak_ref_clear (&self->core);

  G_OBJECT_CLASS (wp_conf_parent_class)->finalize (object);
}

static void
wp_conf_class_init (WpConfClass * klass)
{
  GObjectClass * object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wp_conf_finalize;
  object_class->set_property = wp_conf_set_property;
  object_class->get_property = wp_conf_get_property;

  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The WpCore", WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Returns the WpConf instance that is associated with the
 * given core.
 *
 * This method will also create the instance and register it with the core
 * if it had not been created before.
 *
 * \ingroup wpconf
 * \param core the core
 * \returns (transfer full): the WpConf instance
 */
WpConf *
wp_conf_get_instance (WpCore *core)
{
  WpRegistry *registry = wp_core_get_registry (core);
  WpConf *conf = wp_registry_find_object (registry,
      (GEqualFunc) WP_IS_CONF, NULL);

  if (G_UNLIKELY (!conf)) {
    conf = g_object_new (WP_TYPE_CONF,
        "core", core,
        NULL);

    wp_registry_register_object (registry, g_object_ref (conf));

    wp_info_object (conf, "created wpconf object");
  }

  return conf;
}

static WpSpaJson * merge_json (WpSpaJson *old, WpSpaJson *new);

static WpSpaJson *
merge_json_objects (WpSpaJson *old, WpSpaJson *new)
{
  g_autoptr (WpSpaJsonBuilder) b = NULL;

  g_return_val_if_fail (wp_spa_json_is_object (old), NULL);
  g_return_val_if_fail (wp_spa_json_is_object (new), NULL);

  b = wp_spa_json_builder_new_object ();

  /* Add all properties from 'old' that don't exist in 'new' */
  {
    g_autoptr (WpIterator) it = wp_spa_json_new_iterator (old);
    g_auto (GValue) item = G_VALUE_INIT;
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      g_autoptr (WpSpaJson) key = NULL;
      g_autoptr (WpSpaJson) val = NULL;
      g_autoptr (WpSpaJson) j = NULL;
      g_autofree gchar *str = NULL;
      const gchar *key_str;
      g_autofree gchar *override_key_str = NULL;
      gboolean override;

      key = g_value_dup_boxed (&item);
      key_str = str = wp_spa_json_parse_string (key);
      g_return_val_if_fail (key_str, NULL);
      override = g_str_has_prefix (str, OVERRIDE_SECTION_PREFIX);
      if (override)
        key_str += strlen (OVERRIDE_SECTION_PREFIX);
      override_key_str = g_strdup_printf (OVERRIDE_SECTION_PREFIX "%s", key_str);

      g_value_unset (&item);
      g_return_val_if_fail (wp_iterator_next (it, &item), NULL);
      val = g_value_dup_boxed (&item);

      if (!wp_spa_json_object_get (new, key_str, "J", &j, NULL) &&
          !wp_spa_json_object_get (new, override_key_str, "J", &j, NULL)) {
        wp_spa_json_builder_add_property (b, key_str);
        wp_spa_json_builder_add_json (b, val);
      }
    }
  }

  /* Add properties from 'new' that don't exist in 'old'. If a property
   * exists in 'old' and does not have the 'override.' prefix, recursively
   * merge it before adding it. Otherwise override it. */
  {
    g_autoptr (WpIterator) it = wp_spa_json_new_iterator (new);
    g_auto (GValue) item = G_VALUE_INIT;
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      g_autoptr (WpSpaJson) key = NULL;
      g_autoptr (WpSpaJson) val = NULL;
      g_autoptr (WpSpaJson) j = NULL;
      g_autofree gchar *str = NULL;
      const gchar *key_str;
      g_autofree gchar *override_key_str = NULL;
      gboolean override;

      key = g_value_dup_boxed (&item);
      key_str = str = wp_spa_json_parse_string (key);
      g_return_val_if_fail (key_str, NULL);
      override = g_str_has_prefix (str, OVERRIDE_SECTION_PREFIX);
      if (override)
        key_str += strlen (OVERRIDE_SECTION_PREFIX);
      override_key_str = g_strdup_printf (OVERRIDE_SECTION_PREFIX "%s", key_str);

      g_value_unset (&item);
      g_return_val_if_fail (wp_iterator_next (it, &item), NULL);
      val = g_value_dup_boxed (&item);

      if (!override &&
          (wp_spa_json_object_get (old, key_str, "J", &j, NULL) ||
           wp_spa_json_object_get (old, override_key_str, "J", &j, NULL))) {
        g_autoptr (WpSpaJson) merged = merge_json (j, val);
        if (!merged) {
          wp_warning ("skipping merge of %s as JSON values are not compatible",
              key_str);
          continue;
        }
        wp_spa_json_builder_add_property (b, key_str);
        wp_spa_json_builder_add_json (b, merged);
      } else {
        wp_spa_json_builder_add_property (b, key_str);
        wp_spa_json_builder_add_json (b, val);
      }
    }
  }

  return wp_spa_json_builder_end (b);
}

static WpSpaJson *
merge_json_arrays (WpSpaJson *old, WpSpaJson *new)
{
  g_autoptr (WpSpaJsonBuilder) b = NULL;

  g_return_val_if_fail (wp_spa_json_is_array (old), NULL);
  g_return_val_if_fail (wp_spa_json_is_array (new), NULL);

  b = wp_spa_json_builder_new_array ();

  /* Add all elements from 'old' */
  {
    g_autoptr (WpIterator) it = wp_spa_json_new_iterator (old);
    g_auto (GValue) item = G_VALUE_INIT;
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaJson *j = g_value_get_boxed (&item);
      wp_spa_json_builder_add_json (b, j);
    }
  }

  /* Add all elements from 'new' */
  {
    g_autoptr (WpIterator) it = wp_spa_json_new_iterator (new);
    g_auto (GValue) item = G_VALUE_INIT;
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpSpaJson *j = g_value_get_boxed (&item);
      wp_spa_json_builder_add_json (b, j);
    }
  }

  return wp_spa_json_builder_end (b);
}

static WpSpaJson *
merge_json (WpSpaJson *old, WpSpaJson *new)
{
  if (wp_spa_json_is_array (old) && wp_spa_json_is_array (new))
    return merge_json_arrays (old, new);
  else if (wp_spa_json_is_object (old) && wp_spa_json_is_object (new))
    return merge_json_objects (old, new);
  return NULL;
}

static gint
merge_section_cb (void *data, const char *location, const char *section,
    const char *str, size_t len)
{
  WpSpaJson **res_section = (WpSpaJson **)data;
  g_autoptr (WpSpaJson) json = NULL;
  gboolean override;

  g_return_val_if_fail (res_section, -EINVAL);

  override = g_str_has_prefix (section, OVERRIDE_SECTION_PREFIX);
  if (override)
    section += strlen (OVERRIDE_SECTION_PREFIX);

  wp_debug ("loading section %s (override=%d) from %s", section, override,
      location);

  /* Only allow sections to be objects or arrays */
  json = wp_spa_json_new_from_stringn (str, len);
  if (!wp_spa_json_is_container (json)) {
    wp_warning (
        "skipping section %s from %s as it is not JSON object or array",
        section, location);
    return 0;
  }

  /* Merge section if it was defined previously and the 'override.' prefix is
   * not used */
  if (!override && *res_section) {
    g_autoptr (WpSpaJson) merged = merge_json (*res_section, json);
    if (!merged) {
      wp_warning (
          "skipping merge of %s from %s as JSON values are not compatible",
          section, location);
      return 0;
    }

    g_clear_pointer (res_section, wp_spa_json_unref);
    *res_section = g_steal_pointer (&merged);
    wp_debug ("section %s from %s loaded", location, section);
  }

  /* Otherwise always replace */
  else {
    g_clear_pointer (res_section, wp_spa_json_unref);
    *res_section = g_steal_pointer (&json);
    wp_debug ("section %s from %s loaded", location, section);
  }

  return 0;
}

static void
ensure_section_loaded (WpConf *self, const gchar *section)
{
  g_autoptr (WpCore) core = NULL;
  struct pw_context *pw_ctx = NULL;
  g_autoptr (WpSpaJson) json_section = NULL;
  g_autofree gchar *override_section = NULL;

  if (g_hash_table_contains (self->sections, section))
    return;

  core = g_weak_ref_get (&self->core);
  g_return_if_fail (core);
  pw_ctx = wp_core_get_pw_context (core);
  g_return_if_fail (pw_ctx);

  pw_context_conf_section_for_each (pw_ctx, section, merge_section_cb,
      &json_section);
  override_section = g_strdup_printf (OVERRIDE_SECTION_PREFIX "%s", section);
  pw_context_conf_section_for_each (pw_ctx, override_section, merge_section_cb,
      &json_section);

  if (json_section)
    g_hash_table_insert (self->sections, g_strdup (section),
        g_steal_pointer (&json_section));
}

/*!
 * This method will get the JSON value of a specific section from the
 * configuration. If the same section is defined in multiple locations, the
 * sections with the same name will be either merged in case of arrays and
 * objects, or overridden in case of boolean, int, double and strings. The
 * passed fallback value will be returned if the section does not exist.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section the section name
 * \param fallback (transfer full)(nullable): the fallback value
 * \returns (transfer full): the JSON value of the section
 */
WpSpaJson *
wp_conf_get_section (WpConf *self, const gchar *section, WpSpaJson *fallback)
{
  WpSpaJson *s;
  g_autoptr (WpSpaJson) fb = fallback;

  g_return_val_if_fail (WP_IS_CONF (self), NULL);

  ensure_section_loaded (self, section);

  s = g_hash_table_lookup (self->sections, section);
  if (!s)
    return fb ? g_steal_pointer (&fb) : NULL;

  return wp_spa_json_ref (s);
}

/*!
 * This is a convenient function to access a JSON value from an object
 * section in the configuration. If the section is an array, or the key does
 * not exist in the object section, it will return the passed fallback value.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section the section name
 * \param key the key name
 * \param fallback (transfer full)(nullable): the fallback value
 * \returns (transfer full): the JSON value of the section's key if it exists,
 * or the passed fallback value otherwise
 */
WpSpaJson *
wp_conf_get_value (WpConf *self, const gchar *section, const gchar *key,
    WpSpaJson *fallback)
{
  g_autoptr (WpSpaJson) s = NULL;
  g_autoptr (WpSpaJson) fb = fallback;
  WpSpaJson *v;

  g_return_val_if_fail (WP_IS_CONF (self), NULL);
  g_return_val_if_fail (section, NULL);
  g_return_val_if_fail (key, NULL);

  s = wp_conf_get_section (self, section, NULL);
  if (!s)
    goto return_fallback;

  if (!wp_spa_json_is_object (s)) {
    wp_warning_object (self,
        "Cannot get JSON key %s from %s as section is not an JSON object",
        key, section);
    goto return_fallback;
  }

  if (wp_spa_json_object_get (s, key, "J", &v, NULL))
    return v;

return_fallback:
  return fb ? g_steal_pointer (&fb) : NULL;
}

/*!
 * This is a convenient function to access a boolean value from an object
 * section in the configuration. If the section is an array, or the key does
 * not exist in the object section, it will return the passed fallback value.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section the section name
 * \param key the key name
 * \param fallback the fallback value
 * \returns the boolean value of the section's key if it exists and could be
 * parsed, or the passed fallback value otherwise
 */
gboolean
wp_conf_get_value_boolean (WpConf *self, const gchar *section,
    const gchar *key, gboolean fallback)
{
  g_autoptr (WpSpaJson) s = NULL;
  gboolean v;

  g_return_val_if_fail (WP_IS_CONF (self), FALSE);
  g_return_val_if_fail (section, FALSE);
  g_return_val_if_fail (key, FALSE);

  s = wp_conf_get_section (self, section, NULL);
  if (!s)
    return fallback;

  if (!wp_spa_json_is_object (s)) {
    wp_warning_object (self,
        "Cannot get boolean key %s from %s as section is not an JSON object",
        key, section);
    return fallback;
  }

  return wp_spa_json_object_get (s, key, "b", &v, NULL) ? v : fallback;
}

/*!
 * This is a convenient function to access a int value from an object
 * section in the configuration. If the section is an array, or the key does
 * not exist in the object section, it will return the passed fallback value.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section the section name
 * \param key the key name
 * \param fallback the fallback value
 * \returns the int value of the section's key if it exists and could be
 * parsed, or the passed fallback value otherwise
 */
gint
wp_conf_get_value_int (WpConf *self, const gchar *section,
    const gchar *key, gint fallback)
{
  g_autoptr (WpSpaJson) s = NULL;
  gint v;

  g_return_val_if_fail (WP_IS_CONF (self), 0);
  g_return_val_if_fail (section, 0);
  g_return_val_if_fail (key, 0);

  s = wp_conf_get_section (self, section, NULL);
  if (!s)
    return fallback;

  if (!wp_spa_json_is_object (s)) {
    wp_warning_object (self,
        "Cannot get int key %s from %s as section is not an JSON object",
        key, section);
    return fallback;
  }

  return wp_spa_json_object_get (s, key, "i", &v, NULL) ? v : fallback;
}

/*!
 * This is a convenient function to access a float value from an object
 * section in the configuration. If the section is an array, or the key does
 * not exist in the object section, it will return the passed fallback value.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section the section name
 * \param key the key name
 * \param fallback the fallback value
 * \returns the float value of the section's key if it exists and could be
 * parsed, or the passed fallback value otherwise
 */
float
wp_conf_get_value_float (WpConf *self, const gchar *section,
    const gchar *key, float fallback)
{
  g_autoptr (WpSpaJson) s = NULL;
  float v;

  g_return_val_if_fail (WP_IS_CONF (self), 0);
  g_return_val_if_fail (section, 0);
  g_return_val_if_fail (key, 0);

  s = wp_conf_get_section (self, section, NULL);
  if (!s)
    return fallback;

  if (!wp_spa_json_is_object (s)) {
    wp_warning_object (self,
        "Cannot get float key %s from %s as section is not an JSON object",
        key, section);
    return fallback;
  }

  return wp_spa_json_object_get (s, key, "f", &v, NULL) ? v : fallback;
}

/*!
 * This is a convenient function to access a string value from an object
 * section in the configuration. If the section is an array, or the key does
 * not exist in the object section, it will return the passed fallback value.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section the section name
 * \param key the key name
 * \param fallback (nullable): the fallback value
 * \returns (transfer full): the string value of the section's key if it exists
 * and could be parsed, or the passed fallback value otherwise
 */
gchar *
wp_conf_get_value_string (WpConf *self, const gchar *section,
    const gchar *key, const gchar *fallback)
{
  g_autoptr (WpSpaJson) s = NULL;
  gchar *v;

  g_return_val_if_fail (WP_IS_CONF (self), NULL);
  g_return_val_if_fail (section, NULL);
  g_return_val_if_fail (key, NULL);

  s = wp_conf_get_section (self, section, NULL);
  if (!s)
    goto return_fallback;

  if (!wp_spa_json_is_object (s)) {
    wp_warning_object (self,
        "Cannot get string key %s from %s as section is not an JSON object",
        key, section);
    goto return_fallback;
  }

  if (wp_spa_json_object_get (s, key, "s", &v, NULL))
    return v;

return_fallback:
  return fallback ? g_strdup (fallback) : NULL;
}

static gboolean
matches_properties (WpSpaJson *match, WpProperties *props)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  g_return_val_if_fail (match, FALSE);

  if (!wp_spa_json_is_array (match))
    return FALSE;

  it = wp_spa_json_new_iterator (match);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *match = g_value_get_boxed (&item);
    if (match && wp_spa_json_is_object (match)) {
      g_autoptr (WpObjectInterest) interest =
          wp_object_interest_new_type (WP_TYPE_PROPERTIES);
      g_autoptr (WpIterator) it_match = wp_spa_json_new_iterator (match);
      g_auto (GValue) item_match = G_VALUE_INIT;

      for (; wp_iterator_next (it_match, &item_match);
          g_value_unset (&item_match)) {
        WpSpaJson *p;
        g_autofree gchar *key = NULL;
        g_autofree gchar *val = NULL;
        WpConstraintVerb verb = WP_CONSTRAINT_VERB_EQUALS;

        p = g_value_get_boxed (&item_match);
        key = wp_spa_json_parse_string (p);

        g_value_unset (&item_match);
        g_return_val_if_fail (wp_iterator_next (it_match, &item_match), FALSE);
        p = g_value_get_boxed (&item_match);
        val = wp_spa_json_parse_string (p);

        if (val[0] == '~')
          verb = WP_CONSTRAINT_VERB_MATCHES;

        wp_object_interest_add_constraint (interest,
            WP_CONSTRAINT_TYPE_PW_PROPERTY, key, verb,
            g_variant_new_string (val + 1));
      }

      if (wp_object_interest_matches (interest, props))
        return TRUE;
    }
  }

  return FALSE;
}

static gboolean
apply_properties (WpSpaJson *update_props, WpProperties *applied_props)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  g_return_val_if_fail (update_props, FALSE);
  g_return_val_if_fail (applied_props, FALSE);

  if (!wp_spa_json_is_object (update_props))
    return FALSE;

  it = wp_spa_json_new_iterator (update_props);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    g_autoptr (WpSpaJson) key = NULL;
    g_autoptr (WpSpaJson) val = NULL;
    g_autofree gchar *key_str = NULL;
    g_autofree gchar *val_str = NULL;

    key = g_value_dup_boxed (&item);
    key_str = wp_spa_json_parse_string (key);
    g_return_val_if_fail (key_str, FALSE);

    g_value_unset (&item);
    g_return_val_if_fail (wp_iterator_next (it, &item), FALSE);
    val = g_value_dup_boxed (&item);
    val_str = wp_spa_json_parse_string (val);
    g_return_val_if_fail (val_str, FALSE);

    wp_properties_set (applied_props, key_str, val_str);
  }

  return TRUE;
}

static gboolean
apply_rules_json (WpSpaJson *rules, WpProperties *match_props,
    WpProperties *applied_props)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  g_return_val_if_fail (rules, FALSE);
  g_return_val_if_fail (match_props, FALSE);

  it = wp_spa_json_new_iterator (rules);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    g_autoptr (WpSpaJson) match = NULL;
    g_autoptr (WpSpaJson) update_props = NULL;
    WpSpaJson *rule = g_value_get_boxed (&item);
    if (rule &&
        wp_spa_json_is_object (rule) &&
        wp_spa_json_object_get (rule,
            "matches", "J", &match,
            "update-props", "J", &update_props,
            NULL) &&
        matches_properties (match, match_props)) {
      if (!applied_props)
        return apply_properties (update_props, match_props);

      wp_properties_update (applied_props, match_props);
      return apply_properties (update_props, applied_props);
    }
  }

  return FALSE;
}

/*!
 * This function applies the rules on a given matched properties. If the
 * applied_props param is not NULL, The applied rules are copied into
 * applied_props and matched_props is not altered. The fallback rules are only
 * used if the rules section cannot be found in the configuration, or the
 * section is not an JSON array.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section name of the configuration section that has the rules
 * \param match_props (transfer none)(inout): the properties to match the rules
 * \param applied_props (transfer none)(nullable)(out): the properties with
 * the rules applied
 * \param fallback (transfer full)(nullable): The fallback rules to apply if the
 * rules section could not be found in the configuration, or the section is not
 * a JSON array.
 * \returns TRUE if rules were applied, FALSE otherwise
 */
gboolean
wp_conf_apply_rules (WpConf *self, const gchar *section,
    WpProperties *match_props, WpProperties *applied_props, WpSpaJson *fallback)
{
  g_autoptr (WpSpaJson) s = NULL;
  g_autoptr (WpSpaJson) fb = fallback;

  g_return_val_if_fail (WP_IS_CONF (self), FALSE);
  g_return_val_if_fail (section, FALSE);
  g_return_val_if_fail (match_props, FALSE);

  s = wp_conf_get_section (self, section, NULL);
  if (!s || !wp_spa_json_is_array (s))
    return fb ? apply_rules_json (fb, match_props, applied_props) : FALSE;

  return apply_rules_json (s, match_props, applied_props);
}
