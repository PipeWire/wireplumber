/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-settings"

#include <wp/wp.h>

#include "settings.h"
#include "metadata.h"
#include "log.h"
#include "private/registry.h"

/*! \defgroup wpsetttings WpSettings */
/*!
 * \struct WpSettings
 *
 * WpSettings loads and parses `sm-settings`(default value) metadata(contains
 * wireplumber settings and rules). It provides APIs to its clients(modules,
 * lua scripts etc) to access and change them.
 *
 * Being a WpObject subclass, the settings inherits WpObject's activation
 * system.
 *
 */

struct _WpSettings
{
  WpObject parent;

  WpProperties *settings;

  /* element-type: Rule* */
  GPtrArray *rules;

  gchar *metadata_name;
  WpObjectManager *metadata_om;
};

typedef struct
{
  gchar *rule;
  /* element-type: Match* */
  GPtrArray *matches;
} Rule;

typedef struct
{
  /* element-type: WpObjectInterest* */
  GPtrArray *interests;
  WpProperties *actions;
} Match;

enum {
  PROP_0,
  PROP_METADATA_NAME,
  PROP_PROPERTIES,
};

G_DEFINE_TYPE (WpSettings, wp_settings, WP_TYPE_OBJECT)

static void
wp_settings_init (WpSettings * self)
{
}


/*!
 * \brief gets the boolean value of a setting.
 *
 * \ingroup wpsetting
 * \param self the handle
 * \param setting name of the setting
 * \param value(out): the boolean value of the setting
 * \returns:  TRUE if the setting is defined, FALSE otherwise
 */
gboolean
wp_settings_get_boolean (WpSettings *self, const gchar *setting,
  gboolean *value)
{
  g_return_val_if_fail (self, false);
  g_return_val_if_fail (setting, false);

  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
          WP_OBJECT_FEATURES_ALL))
    return false;

  if (!wp_properties_get (self->settings, setting))
    return false;

  *value = spa_atob (wp_properties_get (self->settings, setting));

  return true;
}

/*!
 * \brief gets the string value of a setting.
 *
 * \ingroup wpsetting
 * \param self the handle
 * \param setting name of the setting
 * \param value(out): the string value of the setting
 * \returns:  TRUE if the setting is defined, FALSE otherwise
 */
gboolean
wp_settings_get_string (WpSettings *self, const gchar *setting,
    const char **value)
{
  g_return_val_if_fail (self, false);
  g_return_val_if_fail (setting, false);

  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
          WP_OBJECT_FEATURES_ALL))
    return false;

  if (!wp_properties_get (self->settings, setting))
    return false;

  *value = wp_properties_get (self->settings, setting);

  return true;
}

/*!
 * \brief gets the integer(signed) value of a setting.
 *
 * \ingroup wpsetting
 * \param self the handle
 * \param value(out): the integer value of the setting
 * \returns:  TRUE if the setting is defined, FALSE otherwise
 */
gboolean
wp_settings_get_int (WpSettings *self, const gchar *setting,
    gint64 *val)
{
  g_return_val_if_fail (self, false);
  g_return_val_if_fail (setting, false);

  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
          WP_OBJECT_FEATURES_ALL))
    return false;

  if (!wp_properties_get (self->settings, setting))
    return false;

  *val = 0; /* ground the value */
  spa_atoi64 (wp_properties_get (self->settings, setting), val, 0);

  return true;
}

/*!
 * \brief applies the rules and returns the applied props.
 *
 * This funtion applies the rules on the client properties and if
 * there is a match, returns true and also copies the applied props.
 *
 * \ingroup wpsetting
 * \param self the handle
 * \param rule name of the rule, this will match with the section mentioned
 *  in the conf file.
 * \param client_props client props array, these properties are inputs on which
 *  the rules are applied.
 * \param applied_props (nullable) the resultant actions/properties as a result
 *  of the application of rules are copied, if this is null props will be
 *  appended to client_props.
 * \returns TRUE if there is a match for the client_props and returns the
 *  applied props for the match.
 */
gboolean
wp_settings_apply_rule (WpSettings *self, const gchar *rule,
    WpProperties *client_props, WpProperties *applied_props)
{
  g_return_val_if_fail (self, false);
  g_return_val_if_fail (rule, false);
  g_return_val_if_fail (client_props, false);

  wp_debug_object (self, "applying rule(%s) for client props", rule);

  for (guint i = 0; i < self->rules->len; i++) {
    Rule *r = g_ptr_array_index (self->rules, i);

    if (g_str_equal (rule, r->rule)) {
      for (guint j = 0; j < r->matches->len; j++) {
        Match *m = g_ptr_array_index (r->matches, j);

        for (guint k = 0; k < m->interests->len; k++) {
          WpObjectInterest *interest = g_ptr_array_index (m->interests, k);

          wp_debug_object (self, ". working on interest obj(%p)", interest);

          if (wp_object_interest_matches (interest, client_props)) {
            if (applied_props)
              wp_properties_add (applied_props, m->actions);
            else
              wp_properties_add (client_props, m->actions);

            wp_debug_object (self, "match found for rule(%s) with actions"
                "(%d)", rule, wp_properties_get_count(m->actions));

            return TRUE;
          }
        }
      }
    }
  }

  return FALSE;
}

enum {
  STEP_LOAD = WP_TRANSITION_STEP_CUSTOM_START,
};

static WpObjectFeatures
wp_settings_get_supported_features (WpObject * self)
{
  return WP_SETTINGS_LOADED;
}

static guint
wp_settings_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_SETTINGS_LOADED,
      WP_TRANSITION_STEP_ERROR);

  return STEP_LOAD;
}

static gboolean
check_metadata_name (gpointer  g_object, gpointer  metadata_name)
{
  if (!WP_IS_SETTINGS(g_object))
    return false;

  g_auto (GValue) value = G_VALUE_INIT;
  g_object_get_property (G_OBJECT(g_object), "metadata-name", &value);

  return g_str_equal (g_value_get_string (&value), (gchar *)metadata_name);
}


/*!
 * \brief Returns the wpsettings instance that is associated with the
 * given core.
 *
 * This method will also create the instance and register it with the core
 * if it had not been created before.
 *
 * \ingroup wpsetting
 * \param core the core
 * \param metadata_name (nullable) the name of the metadata with which this
 *    object should is associated. `sm-settings` is the default value picked if
 *    none is supplied.
 * \returns:  (transfer full) the wpsettings instance
 */
WpSettings *
wp_settings_get_instance (WpCore *core, const gchar *metadata_name)
{
  WpRegistry *registry = wp_core_get_registry (core);
  const gchar *name = (metadata_name ? metadata_name : "sm-settings") ;
  WpSettings *settings = wp_registry_find_object (registry,
      (GEqualFunc) check_metadata_name, name);

  if (G_UNLIKELY (!settings)) {
    settings = g_object_new (WP_TYPE_SETTINGS,
        "core", core,
        "metadata-name", name,
        NULL);

    wp_registry_register_object (registry, g_object_ref (settings));

    wp_info_object (settings, "created wpsettings object for metadata"
      " name \"%s\"", name);
  } else {
    wp_info_object (settings, "found this wpsettings object for metadata name"
        " \"%s\"", name);
  }
  return settings;
}

static void
match_unref (Match * self)
{
  g_clear_pointer (&self->actions, wp_properties_unref);
  g_clear_pointer (&self->interests, g_ptr_array_unref);
}

static WpProperties *
parse_actions (const gchar *actions)
{
  g_autoptr (WpSpaJson) o = wp_spa_json_new_from_string (actions);
  g_autofree gchar *update_props = NULL;
  g_autoptr (WpProperties) a_props = wp_properties_new_empty ();

  wp_debug(".. parsing actions");

  if (wp_spa_json_is_object (o) &&
      wp_spa_json_object_get (o,
          "update-props", "s", &update_props,
          NULL)) {
    g_autoptr (WpSpaJson) json = wp_spa_json_new_from_string (update_props);
    g_autoptr (WpIterator) iter = wp_spa_json_new_iterator (json);
    g_auto (GValue) item = G_VALUE_INIT;

    wp_debug (".. update-props=%s", update_props);

    while (wp_iterator_next (iter, &item)) {
      WpSpaJson *p = g_value_get_boxed (&item);
      g_autofree gchar *prop = wp_spa_json_parse_string (p);
      g_autofree gchar *value = NULL;

      g_value_unset (&item);
      wp_iterator_next (iter, &item);
      p = g_value_get_boxed (&item);

      value = wp_spa_json_parse_string (p);
      g_value_unset (&item);

      if (prop && value) {
        wp_debug (".. prop=%s value=%s", prop, value);
        wp_properties_set (a_props, prop, value);
      }
    }
  } else {
    wp_warning ("malformated JSON: \"update-props\" not defined properly"
        ", skip it");
    return NULL;
  }

  return g_steal_pointer (&a_props);
}

static Match *
parse_matches (const gchar *match)
{
  g_autoptr (WpSpaJson) a = wp_spa_json_new_from_string (match);
  g_autoptr (WpIterator) a_iter = wp_spa_json_new_iterator (a);
  g_auto (GValue) a_item = G_VALUE_INIT;
  Match *m = g_slice_new0 (Match);

  g_return_val_if_fail (m, NULL);

  wp_debug(".. parsing match");
  m->interests = g_ptr_array_new_with_free_func
      ((GDestroyNotify) wp_object_interest_unref);

  if (!wp_spa_json_is_array (a))
  {
    wp_warning ("malformated JSON: matches has to be an array JSON element"
        ", skip processing this one");
    return NULL;
  }

  for (; wp_iterator_next (a_iter, &a_item); g_value_unset (&a_item)) {
    g_autoptr (WpObjectInterest) i = wp_object_interest_new_type
      (WP_TYPE_PROPERTIES);
    WpSpaJson *o = g_value_get_boxed (&a_item);
    WpIterator *o_iter = wp_spa_json_new_iterator (o);
    g_auto (GValue) o_item = G_VALUE_INIT;
    int count = 0;

    while (wp_iterator_next (o_iter, &o_item)) {
      WpSpaJson *p = g_value_get_boxed (&o_item);
      if (wp_spa_json_is_container (p))
      {
        wp_warning ("malformated JSON: misplaced container object, pls check"
          " JSON formatting of .conf file, skipping this container");
        continue;
      }
      g_autofree gchar *isubject = wp_spa_json_parse_string (p);
      g_autofree gchar *value = NULL;
      gchar *ivalue = NULL;
      WpConstraintVerb iverb = WP_CONSTRAINT_VERB_EQUALS;

      g_value_unset (&o_item);
      wp_iterator_next (o_iter, &o_item);
      p = g_value_get_boxed (&o_item);

      ivalue = value = wp_spa_json_parse_string (p);
      g_value_unset (&o_item);

      if (value[0] == '~')
      {
        iverb = WP_CONSTRAINT_VERB_MATCHES;
        ivalue = value+1;
      }
      if (isubject && ivalue) {
        wp_object_interest_add_constraint (i, WP_CONSTRAINT_TYPE_PW_PROPERTY,
            isubject, iverb, g_variant_new_string(ivalue));
        count++;
        wp_debug (".. subject=%s verb=%d value=%s of interest obj=%p",
            isubject, iverb, ivalue, i);
      }
    }
    wp_debug (".. loaded interest obj(%p) with (%d) constraints", i, count);
    g_ptr_array_add (m->interests, g_steal_pointer(&i));
  }
  return m;
}

static Rule *
parse_rule (const gchar *rule, const gchar *value)
{
  g_autoptr (WpSpaJson) json = wp_spa_json_new_from_string (value);
  g_autoptr (WpIterator) iter = wp_spa_json_new_iterator (json);
  g_auto (GValue) item = G_VALUE_INIT;
  Rule *r = g_slice_new0 (Rule);

  g_return_val_if_fail (r, NULL);

  /* TBD: check for duplicate rule names and disallow them. */
  r->rule = g_strdup (rule);

  wp_debug (". parsing rule(%s)", r->rule);
  r->matches = g_ptr_array_new_with_free_func
      ((GDestroyNotify) match_unref);

  for (; wp_iterator_next (iter, &item); g_value_unset (&item)) {
    WpSpaJson *o = g_value_get_boxed (&item);
    g_autofree gchar *match = NULL;
    g_autofree gchar *actions = NULL;
    Match *m = NULL;

    if (!wp_spa_json_is_object (o) ||
        !wp_spa_json_object_get (o,
            "matches", "s", &match,
            "actions", "s", &actions,
            NULL)) {
      wp_warning ("malformated JSON: either JSON object is not found or "
          "an empty object with out matches or actions found, skipping it");
      continue;
    }

    m = parse_matches (match);
    g_ptr_array_add (r->matches, m);
    wp_debug (". loaded (%d) interest objects for this match for rule(%s)",
        m->interests->len, r->rule);

    m->actions = parse_actions (actions);
    wp_debug (". loaded (%d) actions for this match for rule(%s)",
        wp_properties_get_count (m->actions), r->rule);
  }

  return r;
}

static void
parse_setting (const gchar *setting, const gchar *value, WpSettings *self)
{
  g_autoptr (WpSpaJson) json = wp_spa_json_new_from_string (value);

  if (!wp_spa_json_is_array (json))
    wp_properties_set (self->settings, setting, value);
  else {
    Rule *r = parse_rule (setting, value);
    g_ptr_array_add (self->rules, r);
    wp_debug_object (self, "loaded (%d) matches for rule (%s)",
        r->matches->len, r->rule);
  }
}

static void
on_metadata_added (WpObjectManager *om, WpMetadata *m, gpointer d)
{
  WpTransition * transition = WP_TRANSITION (d);
  WpSettings * self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = wp_metadata_new_iterator (WP_METADATA (m), 0);
  g_auto (GValue) val = G_VALUE_INIT;

  /* traverse through all settings and rules */
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      const gchar *setting, *value;
      wp_metadata_iterator_item_extract (&val, NULL, &setting, NULL, &value);
      parse_setting (setting, value, self);
  }

  wp_info_object (self, "loaded %d settings and %d rules from metadata \"%s\"",
      wp_properties_get_count (self->settings),
      self->rules->len,
      self->metadata_name);

  wp_object_update_features (WP_OBJECT (self), WP_SETTINGS_LOADED, 0);
}

static void
rule_unref (Rule * self)
{
  g_free (self->rule);
  g_clear_pointer (&self->matches, g_ptr_array_unref);
}

static void
wp_settings_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpSettings * self = WP_SETTINGS (object);
  g_autoptr (WpCore) core = wp_object_get_core (object);

  switch (step) {
  case STEP_LOAD: {

    self->settings = wp_properties_new_empty ();

    self->rules = g_ptr_array_new_with_free_func
        ((GDestroyNotify) rule_unref);

    self->metadata_om = wp_object_manager_new ();
    wp_object_manager_add_interest (self->metadata_om, WP_TYPE_METADATA,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s",
            self->metadata_name, NULL);
    wp_object_manager_request_object_features (self->metadata_om,
        WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
    g_signal_connect_object (self->metadata_om, "object-added",
        G_CALLBACK (on_metadata_added), transition, 0);
    wp_core_install_object_manager (core, self->metadata_om);

    wp_info_object (self, "looking for metadata object named %s",
        self->metadata_name);
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_settings_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpSettings *self = WP_SETTINGS (object);

  g_free (self->metadata_name);
  g_clear_object (&self->metadata_om);
  g_clear_pointer (&self->rules, g_ptr_array_unref);
  g_clear_pointer (&self->settings, wp_properties_unref);

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}


static void
wp_settings_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSettings *self = WP_SETTINGS (object);

  switch (property_id) {
  case PROP_METADATA_NAME:
    self->metadata_name = g_strdup (g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_settings_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpSettings *self = WP_SETTINGS (object);

  switch (property_id) {
  case PROP_METADATA_NAME:
    g_value_set_string (value, self->metadata_name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_settings_class_init (WpSettingsClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;

  object_class->set_property = wp_settings_set_property;
  object_class->get_property = wp_settings_get_property;

  wpobject_class->activate_get_next_step = wp_settings_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_settings_activate_execute_step;
  wpobject_class->deactivate = wp_settings_deactivate;
  wpobject_class->get_supported_features = wp_settings_get_supported_features;

  g_object_class_install_property (object_class, PROP_METADATA_NAME,
      g_param_spec_string ("metadata-name", "metadata-name",
          "The metadata object to look after", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}
