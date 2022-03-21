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

/*! \defgroup WpSettings */
/*!
 * \struct WpSettings
 *
 * WpSettings parses `sm-settings` metadata(contains wireplumber settings
 * and rules), provides APIs to its clients(modules, lua scripts etc) to
 * access and change them.
 *
 * Being a WpObject subclass, the settings inherits WpObject's activation
 * system.
 *
 */

struct _WpSettings
{
  WpObject parent;

  GWeakRef core;

  WpProperties *settings;
  WpObjectManager *metadata_om;
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
 * \returns (transfer none): boolean value of the string.
 */
gboolean wp_settings_get_boolean (WpSettings *self, const gchar *setting)
{
  g_return_val_if_fail (self, false);
  g_return_val_if_fail (setting, false);

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
 * \param setting name of the setting
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


/*!
 * \brief Returns the wpsettings instance that is associated with the
 * given core.
 *
 * This method will also create the instance and register it with the core
 * if it had not been created before.
 *
 * \param core the core
 * \return (transfer full): the wpsettings instance
 */
WpSettings *
wp_settings_get_instance (WpCore * core)
{
  WpRegistry *registry = wp_core_get_registry (core);
  WpSettings *settings = wp_registry_find_object (registry,
      (GEqualFunc) WP_IS_SETTINGS, NULL);

  if (G_UNLIKELY (!settings)) {
    settings = g_object_new (WP_TYPE_SETTINGS,
        "core", core,
        NULL);
    g_weak_ref_set (&settings->core, core);

    wp_registry_register_object (registry, g_object_ref (settings));
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

  wp_info_object (self, "loaded %d settings from metadata",
      wp_properties_get_count (self->settings));

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
            "sm-settings", NULL);
    wp_object_manager_request_object_features (self->metadata_om,
        WP_TYPE_METADATA, WP_OBJECT_FEATURES_ALL);
    g_signal_connect_object (self->metadata_om, "object-added",
        G_CALLBACK (on_metadata_added), transition, 0);
    wp_core_install_object_manager (core, self->metadata_om);

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

  g_clear_pointer (&self->settings, wp_properties_unref);
  g_clear_object (&self->metadata_om);

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_settings_class_init (WpSettingsClass * klass)
{
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;

  wpobject_class->activate_get_next_step = wp_settings_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_settings_activate_execute_step;
  wpobject_class->deactivate = wp_settings_deactivate;
  wpobject_class->get_supported_features = wp_settings_get_supported_features;
}
