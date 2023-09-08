/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "core.h"
#include "settings.h"
#include "metadata.h"
#include "log.h"
#include "object-manager.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-settings")

/*! \defgroup wpsettings WpSettings */
/*!
 * \struct WpSettings
 *
 * WpSettings loads and parses the "sm-settings" (default value) metadata, which
 * contains wireplumber settings. It provides APIs to its clients (modules, lua
 * scripts etc) to access and listen to them.
 *
 * Being a WpObject subclass, the settings inherits WpObject's activation
 * system.
 */

struct _WpSettings
{
  WpObject parent;

  WpProperties *settings;

  /* element-type: Callback* */
  GPtrArray *callbacks;

  gchar *metadata_name;
  WpObjectManager *metadata_om;
};

typedef struct
{
  GClosure *closure;
  gchar *pattern;
} Callback;

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
 * \brief Subscribes callback for a given setting pattern(a glob-style pattern
 * matched using g_pattern_match_simple), this allows clients to look
 * for any changes made in settings through metadata.
 *
 * \ingroup wpsettings
 * \param self the settings object
 * \param pattern name of the pattern to match the settings with
 * \param callback (scope async): the callback triggered when the settings
 *  change.
 * \param user_data data to pass to \a callback
 * \returns the subscription ID (always greater than 0 for successful
 *  subscriptions)
 */
guintptr
wp_settings_subscribe (WpSettings *self,
    const gchar *pattern, WpSettingsChangedCallback callback,
    gpointer user_data)
{
  return wp_settings_subscribe_closure (self, pattern,
      g_cclosure_new (G_CALLBACK (callback), user_data, NULL));
}

/*!
 * \brief Subscribes callback for a given setting pattern(a glob-style pattern
 * matched using g_pattern_match_simple), this allows clients to look
 * for any changes made in settings through metadata.
 *
 * \ingroup wpsettings
 * \param self the settings object
 * \param pattern name of the pattern to match the settings with
 * \param closure (nullable): a GAsyncReadyCallback wrapped in a GClosure
 * \returns the subscription ID (always greater than 0 for success)
 */
guintptr
wp_settings_subscribe_closure (WpSettings *self, const gchar *pattern,
    GClosure *closure)
{
  g_return_val_if_fail (WP_IS_SETTINGS (self), 0);
  g_return_val_if_fail (pattern, 0);
  g_return_val_if_fail (closure, 0);

  Callback *cb = g_slice_new0 (Callback);
  g_return_val_if_fail (cb, 0);

  cb->closure = g_closure_ref (closure);
  g_closure_sink (closure);
  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    g_closure_set_marshal (closure, g_cclosure_marshal_generic);

  cb->pattern = g_strdup (pattern);

  g_ptr_array_add (self->callbacks, cb);

  wp_debug_object (self, "callback(%p) subscribed for pattern(%s)",
      (void *) cb, pattern);

  return (guintptr) cb;
}

/*!
 * \brief Unsubscribes callback for a given subscription_id.
 *
 * \ingroup wpsettings
 * \param self the settings object
 * \param subscription_id identifies the callback
 * \returns TRUE if success, FALSE otherwise
 */
gboolean
wp_settings_unsubscribe (WpSettings *self, guintptr subscription_id)
{
  gboolean ret = FALSE;
  g_return_val_if_fail (WP_IS_SETTINGS (self), FALSE);
  g_return_val_if_fail (subscription_id, FALSE);

  Callback *cb = (Callback *) subscription_id;

  ret = g_ptr_array_remove (self->callbacks, cb);

  wp_debug_object (self, "callback(%p) unsubscription %s", (void *) cb,
      (ret)? "succeeded": "failed");

  return ret;
}

/*!
 * \brief Gets the WpSpaJson of a setting
 * \ingroup wpsettings
 * \param self the settings object
 * \param setting name of the setting
 * \returns (transfer full) (nullable): The WpSpaJson of the setting, or NULL
 * if the setting does not exist
 */
WpSpaJson *
wp_settings_get (WpSettings *self, const gchar *setting)
{
  const gchar *value;

  g_return_val_if_fail (WP_IS_SETTINGS (self), NULL);
  g_return_val_if_fail (setting, NULL);

  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
      WP_OBJECT_FEATURES_ALL))
    return NULL;

  value = wp_properties_get (self->settings, setting);
  return value ? wp_spa_json_new_from_string (value) : NULL;
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
    return FALSE;

  g_auto (GValue) value = G_VALUE_INIT;
  g_object_get_property (G_OBJECT(g_object), "metadata-name", &value);

  return g_str_equal (g_value_get_string (&value), (gchar *)metadata_name);
}


/*!
 * \brief Returns the WpSettings instance that is associated with the
 * given core.
 *
 * This method will also create the instance and register it with the core
 * if it had not been created before.
 *
 * \ingroup wpsettings
 * \param core the core
 * \param metadata_name (nullable): the name of the metadata with which this
 *    object is associated. `sm-settings` is the default value picked if
 *    NULL is supplied.
 * \returns (transfer full): the WpSettings instance
 */
WpSettings *
wp_settings_get_instance (WpCore *core, const gchar *metadata_name)
{
  const gchar *name = (metadata_name ? metadata_name : "sm-settings") ;
  WpSettings *settings = wp_core_find_object (core,
      (GEqualFunc) check_metadata_name, name);

  if (G_UNLIKELY (!settings)) {
    settings = g_object_new (WP_TYPE_SETTINGS,
        "core", core,
        "metadata-name", name,
        NULL);

    wp_core_register_object (core, g_object_ref (settings));

    wp_info_object (settings, "created wpsettings object for metadata"
      " name \"%s\"", name);
  }

  return settings;
}

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
   const gchar *setting, const gchar *type, const gchar *new_value, gpointer d)
{
  WpSettings *self = WP_SETTINGS(d);
  const gchar *old_value = NULL;

  /* Only handle JSON metadata values */
  if (!g_str_equal (type, "Spa:String:JSON"))
    return;

  old_value = wp_properties_get (self->settings, setting);
  if (!old_value) {
    wp_info_object (self, "new setting defined \"%s\" = \"%s\"",
        setting, new_value);
  } else {
    wp_info_object (self, "setting \"%s\" new_value changed from \"%s\" ->"
        " \"%s\"", setting, old_value, new_value);
  }

  wp_properties_set (self->settings, setting, new_value);

  for (guint i = 0; i < self->callbacks->len; i++) {
    Callback *cb = g_ptr_array_index (self->callbacks, i);

    if (g_pattern_match_simple (cb->pattern, setting)) {
      g_autoptr (WpSpaJson) json = NULL;
      GValue values[3] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };

      g_value_init (&values[0], G_TYPE_OBJECT);
      g_value_init (&values[1], G_TYPE_STRING);
      g_value_init (&values[2], WP_TYPE_SPA_JSON);

      g_value_set_object (&values[0], self);
      g_value_set_string (&values[1], setting);
      json = new_value ? wp_spa_json_new_from_string (new_value) : NULL;
      g_value_set_boxed (&values[2], json);

      g_closure_invoke (cb->closure, NULL, 3, values, NULL);

      g_value_unset (&values[0]);
      g_value_unset (&values[1]);
      g_value_unset (&values[2]);

      wp_debug_object (self, "triggered callback(%p)", cb);
    }
  }
}

static void
on_metadata_added (WpObjectManager *om, WpMetadata *m, gpointer d)
{
  WpTransition * transition = WP_TRANSITION (d);
  WpSettings * self = wp_transition_get_source_object (transition);
  g_autoptr (WpIterator) it = wp_metadata_new_iterator (WP_METADATA (m), 0);
  g_auto (GValue) val = G_VALUE_INIT;

  /* Handle the changed signal */
  g_signal_connect_object (m, "changed", G_CALLBACK (on_metadata_changed),
      self, 0);

  /* traverse through all settings */
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    const gchar *setting, *value;
    wp_metadata_iterator_item_extract (&val, NULL, &setting, NULL, &value);
    wp_properties_set (self->settings, setting, value);
  }

  wp_info_object (self, "loaded %d settings and from metadata \"%s\"",
      wp_properties_get_count (self->settings),
      self->metadata_name);

  wp_object_update_features (WP_OBJECT (self), WP_SETTINGS_LOADED, 0);
}

static void
callback_unref (Callback * self)
{
  g_free (self->pattern);
  g_clear_pointer (&self->closure, g_closure_unref);
  g_slice_free (Callback, self);
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

    self->callbacks = g_ptr_array_new_with_free_func
        ((GDestroyNotify) callback_unref);

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

  wp_debug_object (self, "%s", self->metadata_name);
  g_free (self->metadata_name);
  g_clear_object (&self->metadata_om);
  g_clear_pointer (&self->callbacks, g_ptr_array_unref);
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
    self->metadata_name = g_value_dup_string (value);
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
