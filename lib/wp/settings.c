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

  gchar *metadata_name;

  WpProperties *settings;
  WpObjectManager *metadata_om;

};

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
 * \brief gets the value of a setting.
 *
 * \ingroup wpsetting
 * \param self the handle
 * \param setting name of the setting
 * \returns:  (transfer none) boolean value of the string.
 */
gboolean wp_settings_get_boolean (WpSettings *self, const gchar *setting)
{
  g_return_val_if_fail (self, false);
  g_return_val_if_fail (setting, false);

  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
          WP_OBJECT_FEATURES_ALL))
    return false;

  return spa_atob (wp_properties_get (self->settings, setting));
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
 * \param applied_props the resultant actions/properties as a result of the
 *  application of rules are copied here.
 * \returns TRUE if there is a match for the client_props and
 * returns the applied props for the match.
 */
gboolean wp_settings_apply_rule (WpSettings *self, const gchar *rule,
    WpProperties *client_props, WpProperties *applied_props)
{
  /* get the rule */
  return true;
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

static
gboolean check_metadata_name (gpointer  g_object,
    gpointer  metadata_name)
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

    wp_debug_object (settings, "created wpsettings object for metadata"
      " name \"%s\"", name);
  } else {
    wp_debug_object (settings, "found this wpsettings object for metadata name"
        " \"%s\"", name);
  }
  return settings;
}

static void
on_metadata_added (WpObjectManager *om, WpMetadata *m, gpointer d)
{
  WpTransition * transition = WP_TRANSITION (d);
  WpSettings * self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = wp_metadata_new_iterator (WP_METADATA (m), 0);
  g_auto (GValue) val = G_VALUE_INIT;

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
      const gchar *setting, *value;
      wp_metadata_iterator_item_extract (&val, NULL, &setting, NULL, &value);
      wp_properties_set (self->settings, setting, value);
      wp_debug_object (self, "%s(%lu) = %s", setting, strlen(value), value);
  }

  wp_info_object (self, "loaded %d settings from metadata \"%s\"",
      wp_properties_get_count (self->settings), self->metadata_name);

  wp_object_update_features (WP_OBJECT (self), WP_SETTINGS_LOADED, 0);
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

    self->metadata_om = wp_object_manager_new ();
    wp_object_manager_add_interest (self->metadata_om, WP_TYPE_METADATA,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s",
            self->metadata_name, NULL);
    wp_object_manager_request_object_features (self->metadata_om,
        WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
    g_signal_connect_object (self->metadata_om, "object-added",
        G_CALLBACK (on_metadata_added), transition, 0);
    wp_core_install_object_manager (core, self->metadata_om);

    wp_debug_object (self, "looking for metadata object named %s",
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
  g_clear_pointer (&self->settings, wp_properties_unref);
  g_clear_object (&self->metadata_om);

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
