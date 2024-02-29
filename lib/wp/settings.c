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
 * \struct WpSettingsSpec
 *
 * WpSettingSpec holds the specification of a setting.
 */
struct _WpSettingsSpec {
  grefcount ref;
  gchar *desc;
  WpSettingsSpecType type;
  WpSpaJson *def_value;
  WpSpaJson *min_value;
  WpSpaJson *max_value;
};

G_DEFINE_BOXED_TYPE (WpSettingsSpec, wp_settings_spec, wp_settings_spec_ref,
    wp_settings_spec_unref)

/*!
 * \brief Increases the reference count of a settings spec object
 * \ingroup wpsettings
 * \param self a settings spec object
 * \returns (transfer full): \a self with an additional reference count on it
 */
WpSettingsSpec *
wp_settings_spec_ref (WpSettingsSpec * self)
{
  g_ref_count_inc (&self->ref);
  return self;
}

static void
wp_settings_spec_free (WpSettingsSpec * self)
{
  g_clear_pointer (&self->desc, g_free);
  g_clear_pointer (&self->def_value, wp_spa_json_unref);
  g_clear_pointer (&self->min_value, wp_spa_json_unref);
  g_clear_pointer (&self->max_value, wp_spa_json_unref);
  g_slice_free (WpSettingsSpec, self);
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 * \ingroup wpsettings
 * \param self (transfer full): a settings spec object
 */
void
wp_settings_spec_unref (WpSettingsSpec * self)
{
  if (g_ref_count_dec (&self->ref))
    wp_settings_spec_free (self);
}

static WpSettingsSpec *
wp_settings_spec_new (WpSpaJson * spec_json)
{
  WpSettingsSpec *self;
  g_autofree gchar *desc = NULL;
  g_autofree gchar *type_str = NULL;
  WpSettingsSpecType type = WP_SETTINGS_SPEC_TYPE_UNKNOWN;
  g_autoptr (WpSpaJson) def_value = NULL;
  g_autoptr (WpSpaJson) min_value = NULL;
  g_autoptr (WpSpaJson) max_value = NULL;

  g_return_val_if_fail (spec_json, NULL);

  if (!wp_spa_json_is_object (spec_json))
    return NULL;

  /* Parse mandatory fields */
  if (!wp_spa_json_object_get (spec_json,
      "description", "s", &desc,
      "type", "s", &type_str,
      "default", "J", &def_value,
      NULL))
    return NULL;

  /* Parse type and check if values are correct */
  if (g_str_equal (type_str, "bool")) {
    type = WP_SETTINGS_SPEC_TYPE_BOOL;
    if (!wp_spa_json_is_boolean (def_value))
      return NULL;
  } else if (g_str_equal (type_str, "int")) {
    type = WP_SETTINGS_SPEC_TYPE_INT;
    if (!wp_spa_json_object_get (spec_json,
        "min", "J", &min_value,
        "max", "J", &max_value,
        NULL))
      return NULL;
    if (!wp_spa_json_is_int (def_value) ||
        !min_value || !wp_spa_json_is_int (min_value) ||
        !max_value || !wp_spa_json_is_int (max_value))
      return NULL;
  } else if (g_str_equal (type_str, "float")) {
    type = WP_SETTINGS_SPEC_TYPE_FLOAT;
    if (!wp_spa_json_object_get (spec_json,
        "min", "J", &min_value,
        "max", "J", &max_value,
        NULL))
      return NULL;
    if (!wp_spa_json_is_float (def_value) ||
        !min_value || !wp_spa_json_is_float (min_value) ||
        !max_value || !wp_spa_json_is_float (max_value))
      return NULL;
  } else if (g_str_equal (type_str, "string")) {
    type = WP_SETTINGS_SPEC_TYPE_STRING;
  } else if (g_str_equal (type_str, "array")) {
    type = WP_SETTINGS_SPEC_TYPE_ARRAY;
    if (!wp_spa_json_is_array (def_value))
      return NULL;
  } else if (g_str_equal (type_str, "object")) {
    type = WP_SETTINGS_SPEC_TYPE_OBJECT;
    if (!wp_spa_json_is_object (def_value))
      return NULL;
  } else {
    return NULL;
  }

  self = g_slice_new0 (WpSettingsSpec);
  g_ref_count_init (&self->ref);
  self->desc = g_steal_pointer (&desc);
  self->type = type;
  self->def_value = g_steal_pointer (&def_value);
  self->min_value = g_steal_pointer (&min_value);
  self->max_value = g_steal_pointer (&max_value);
  return self;
}

/*!
 * \brief Gets the description of a settings spec
 * \ingroup wpsettings
 * \param self the settings spec object
 * \returns the description of the settings spec
 */
const gchar *
wp_settings_spec_get_description (WpSettingsSpec * self)
{
  g_return_val_if_fail (self, NULL);
  return self->desc;
}

/*!
 * \brief Gets the type of a settings spec
 * \ingroup wpsettings
 * \param self the settings spec object
 * \returns the type of the settings spec
 */
WpSettingsSpecType
wp_settings_spec_get_value_type (WpSettingsSpec * self)
{
  g_return_val_if_fail (self, WP_SETTINGS_SPEC_TYPE_UNKNOWN);
  return self->type;
}

/*!
 * \brief Gets the default value of a settings spec
 * \ingroup wpsettings
 * \param self the settings spec object
 * \returns (transfer full): the default value of the settings spec
 */
WpSpaJson *
wp_settings_spec_get_default_value (WpSettingsSpec * self)
{
  g_return_val_if_fail (self, NULL);
  g_return_val_if_fail (self->def_value, NULL);
  return wp_spa_json_ref (self->def_value);
}

/*!
 * \brief Gets the minimum value of a settings spec.
 * \ingroup wpsettings
 * \param self the settings spec object
 * \returns (transfer full)(nullable): the minimum value of the settings spec,
 * or NULL if the spec type is not WP_SETTINGS_SPEC_TYPE_INT or
 * WP_SETTINGS_SPEC_TYPE_FLOAT
 */
WpSpaJson *
wp_settings_spec_get_min_value (WpSettingsSpec * self)
{
  g_return_val_if_fail (self, NULL);
  return self->min_value ? wp_spa_json_ref (self->min_value) : NULL;
}

/*!
 * \brief Gets the maximum value of a settings spec.
 * \ingroup wpsettings
 * \param self the settings spec object
 * \returns (transfer full)(nullable): the maximum value of the settings spec,
 * or NULL if the spec type is not WP_SETTINGS_SPEC_TYPE_INT or
 * WP_SETTINGS_SPEC_TYPE_FLOAT
 */
WpSpaJson *
wp_settings_spec_get_max_value (WpSettingsSpec * self)
{
  g_return_val_if_fail (self, NULL);
  return self->max_value ? wp_spa_json_ref (self->max_value) : NULL;
}

/*!
 * \brief Checks whether a value is compatible with the spec or not
 * \ingroup wpsettings
 * \param self the settings spec object
 * \param value (transfer none): the value to check
 * \returns TRUE if the value is compatible with the spec, FALSE otherwise
 */
gboolean
wp_settings_spec_check_value (WpSettingsSpec * self, WpSpaJson *value)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (value, FALSE);

  switch (self->type) {
    case WP_SETTINGS_SPEC_TYPE_BOOL:
      return wp_spa_json_is_boolean (value);
    case WP_SETTINGS_SPEC_TYPE_INT: {
      gint val = 0, min = 0, max = 0;
      if (!wp_spa_json_is_int (value) || !wp_spa_json_parse_int (value, &val))
        return FALSE;
      if (!wp_spa_json_parse_int (self->min_value, &min) ||
          !wp_spa_json_parse_int (self->max_value, &max))
        return FALSE;
      return val >= min && val <= max;
    }
    case WP_SETTINGS_SPEC_TYPE_FLOAT: {
      float val = 0.0, min = 0.0, max = 0.0;
      if (wp_spa_json_is_int (value) || !wp_spa_json_is_float (value) ||
          !wp_spa_json_parse_float (value, &val))
        return FALSE;
      if (!wp_spa_json_parse_float (self->min_value, &min) ||
          !wp_spa_json_parse_float (self->max_value, &max))
        return FALSE;
      return val >= min && val <= max;
    }
    case WP_SETTINGS_SPEC_TYPE_STRING:
      /* We also accept strings without quotes, which is why we dont use
       * wp_spa_json_is_string() */
      return !wp_spa_json_is_boolean (value) && !wp_spa_json_is_int (value) &&
        !wp_spa_json_is_float (value) && !wp_spa_json_is_array (value) &&
        !wp_spa_json_is_object (value);
    case WP_SETTINGS_SPEC_TYPE_ARRAY:
      return wp_spa_json_is_array (value);
    case WP_SETTINGS_SPEC_TYPE_OBJECT:
      return wp_spa_json_is_object (value);
    default:
      break;
  }

  return FALSE;
}

/*!
 * \struct WpSettingsItem
 *
 * WpSettingsItem holds the key and value of a setting
 */
struct _WpSettingsItem
{
  WpMetadata *metadata;
  const gchar *key;
  WpSpaJson *value;
};

G_DEFINE_BOXED_TYPE (WpSettingsItem, wp_settings_item,
    wp_settings_item_ref, wp_settings_item_unref)

static WpSettingsItem *
wp_settings_item_new (WpMetadata *metadata, const gchar *key,
    const gchar *value)
{
  WpSettingsItem *self = g_rc_box_new0 (WpSettingsItem);
  self->metadata = g_object_ref (metadata);
  self->key = key;
  self->value = wp_spa_json_new_from_string (value);
  return self;
}

static void
wp_settings_item_free (gpointer p)
{
  WpSettingsItem *self = p;
  g_clear_pointer (&self->value, wp_spa_json_unref);
  g_clear_object (&self->metadata);
}

/*!
 * \brief Increases the reference count of a settings item object
 * \ingroup wpsettings
 * \param self a settings item object
 * \returns (transfer full): \a self with an additional reference count on it
 */
WpSettingsItem *
wp_settings_item_ref (WpSettingsItem *self)
{
  return g_rc_box_acquire (self);
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 * \ingroup wpsettings
 * \param self (transfer full): a settings item object
 */
void
wp_settings_item_unref (WpSettingsItem *self)
{
  g_rc_box_release_full (self, wp_settings_item_free);
}

/*!
 * \brief Gets the key from a settings item
 *
 * \ingroup wpsettings
 * \param self the item held by the GValue that was returned from the WpIterator
 *   of wp_settings_new_iterator()
 * \returns (transfer none): the settings key of the \a item
 */
const gchar *
wp_settings_item_get_key (WpSettingsItem * self)
{
  return self->key;
}

/*!
 * \brief Gets the value from a settings item
 *
 * \ingroup wpsettings
 * \param self the item held by the GValue that was returned from the WpIterator
 *   of wp_settings_new_iterator()
 * \returns (transfer full): the settings value of the \a item
 */
WpSpaJson *
wp_settings_item_get_value (WpSettingsItem * self)
{
  return wp_spa_json_ref (self->value);
}

/*!
 * \struct WpSettings
 *
 * WpSettings loads and parses the "sm-settings" (default value) metadata, which
 * contains wireplumber settings, and provides APIs to its clients (modules, lua
 * scripts etc) to access them.
 *
 * Being a WpObject subclass, the settings inherits WpObject's activation
 * system.
 */

struct _WpSettings
{
  WpObject parent;

  /* element-type: Callback* */
  GPtrArray *callbacks;

  gchar *metadata_name;
  gchar *metadata_schema_name;
  gchar *metadata_persistent_name;

  WpObjectManager *metadata_om;
  GWeakRef metadata;
  GWeakRef metadata_schema;
  GWeakRef metadata_persistent;
  GHashTable *schema;
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
  g_weak_ref_init (&self->metadata, NULL);
  g_weak_ref_init (&self->metadata_schema, NULL);
  g_weak_ref_init (&self->metadata_persistent, NULL);

  self->schema = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) wp_settings_spec_unref);
}

static void
wp_settings_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSettings *self = WP_SETTINGS (object);

  switch (property_id) {
  case PROP_METADATA_NAME:
    self->metadata_name = g_value_dup_string (value);
    self->metadata_schema_name = g_strdup_printf (
        WP_SETTINGS_SCHEMA_METADATA_NAME_PREFIX "%s", self->metadata_name);
    self->metadata_persistent_name = g_strdup_printf (
        WP_SETTINGS_PERSISTENT_METADATA_NAME_PREFIX "%s", self->metadata_name);
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

static void
on_metadata_changed (WpMetadata *m, guint32 subject,
   const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpSettings *self = WP_SETTINGS(d);

  if (value)
    wp_info_object (self, "setting \"%s\" changed to \"%s\"", key, value);
  else
    wp_info_object (self, "setting \"%s\" removed", key);

  for (guint i = 0; i < self->callbacks->len; i++) {
    Callback *cb = g_ptr_array_index (self->callbacks, i);

    if (g_pattern_match_simple (cb->pattern, key)) {
      g_autoptr (WpSpaJson) json = NULL;
      GValue values[3] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };

      g_value_init (&values[0], G_TYPE_OBJECT);
      g_value_init (&values[1], G_TYPE_STRING);
      g_value_init (&values[2], WP_TYPE_SPA_JSON);

      g_value_set_object (&values[0], self);
      g_value_set_string (&values[1], key);
      json = value ? wp_spa_json_new_wrap_string (value) : NULL;
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
  g_autoptr (WpProperties) props = NULL;
  const gchar *metadata_name = NULL;
  g_autoptr (WpMetadata) metadata = NULL;
  g_autoptr (WpMetadata) metadata_schema = NULL;
  g_autoptr (WpMetadata) metadata_persistent = NULL;

  /* make sure the metadata has a name */
  props = wp_global_proxy_get_global_properties (WP_GLOBAL_PROXY (m));
  if (props)
    metadata_name = wp_properties_get (props, "metadata.name");
  if (!metadata_name)
    return;

  /* sm-settings */
  if (g_str_equal (metadata_name, self->metadata_name)) {
    g_signal_connect_object (m, "changed", G_CALLBACK (on_metadata_changed),
        self, 0);
    g_weak_ref_set (&self->metadata, m);
  }

  /* schema-sm-settings */
  else if (g_str_equal (metadata_name, self->metadata_schema_name)) {
    g_autoptr (WpIterator) it = NULL;
    g_auto (GValue) item = G_VALUE_INIT;
    it = wp_metadata_new_iterator (m, 0);
    for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
      WpMetadataItem *mi = g_value_get_boxed (&item);
      const gchar *key = wp_metadata_item_get_key (mi);
      const gchar *value = wp_metadata_item_get_value (mi);
      g_autoptr (WpSpaJson) spec_json = NULL;
      g_autoptr (WpSettingsSpec) spec = NULL;
      spec_json = wp_spa_json_new_from_string (value);
      spec = wp_settings_spec_new (spec_json);
      if (spec)
        g_hash_table_insert (self->schema, g_strdup (key),
            g_steal_pointer (&spec));
      else
        wp_warning_object (self, "malformed setting spec: %s", value);
    }
    g_weak_ref_set (&self->metadata_schema, m);
  }

  /* presistent-sm-settings */
  else if (g_str_equal (metadata_name, self->metadata_persistent_name)) {
    g_weak_ref_set (&self->metadata_persistent, m);
  }

  /* Finish loading when all metadatas are found */
  metadata = g_weak_ref_get (&self->metadata);
  metadata_schema = g_weak_ref_get (&self->metadata_schema);
  metadata_persistent = g_weak_ref_get (&self->metadata_persistent);
  if (metadata && metadata_schema && metadata_persistent)
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
    self->callbacks = g_ptr_array_new_with_free_func
        ((GDestroyNotify) callback_unref);

    self->metadata_om = wp_object_manager_new ();
    wp_object_manager_add_interest (self->metadata_om, WP_TYPE_METADATA,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s",
            self->metadata_name, NULL);
    wp_object_manager_add_interest (self->metadata_om, WP_TYPE_METADATA,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s",
            self->metadata_schema_name, NULL);
    wp_object_manager_add_interest (self->metadata_om, WP_TYPE_METADATA,
        WP_CONSTRAINT_TYPE_PW_GLOBAL_PROPERTY, "metadata.name", "=s",
            self->metadata_persistent_name, NULL);
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

  g_clear_object (&self->metadata_om);
  g_clear_pointer (&self->callbacks, g_ptr_array_unref);

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_settings_finalize (GObject * object)
{
  WpSettings *self = WP_SETTINGS (object);

  g_clear_pointer (&self->metadata_name, g_free);
  g_clear_pointer (&self->metadata_schema_name, g_free);
  g_clear_pointer (&self->metadata_persistent_name, g_free);

  g_clear_pointer (&self->schema, g_hash_table_unref);

  g_weak_ref_clear (&self->metadata);
  g_weak_ref_clear (&self->metadata_schema);
  g_weak_ref_clear (&self->metadata_persistent);

  G_OBJECT_CLASS (wp_settings_parent_class)->finalize (object);
}

static void
wp_settings_class_init (WpSettingsClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_settings_finalize;
  object_class->set_property = wp_settings_set_property;
  object_class->get_property = wp_settings_get_property;

  wpobject_class->get_supported_features = wp_settings_get_supported_features;
  wpobject_class->activate_get_next_step = wp_settings_activate_get_next_step;
  wpobject_class->activate_execute_step = wp_settings_activate_execute_step;
  wpobject_class->deactivate = wp_settings_deactivate;

  g_object_class_install_property (object_class, PROP_METADATA_NAME,
      g_param_spec_string ("metadata-name", "metadata-name",
          "The metadata object to look after", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Creates a new WpSettings object
 *
 * \ingroup wpsettings
 * \param core the WpCore
 * \param metadata_name (nullable): the name of the metadata object to
 *    associate with the settings object; NULL means the default "sm-settings"
 * \returns (transfer full): a new WpSettings object
 */
WpSettings *
wp_settings_new (WpCore * core, const gchar * metadata_name)
{
  return g_object_new (WP_TYPE_SETTINGS,
      "core", core,
      "metadata-name", metadata_name ? metadata_name : "sm-settings",
      NULL);
}

static gboolean
find_settings_func (gpointer g_object, gpointer metadata_name)
{
  if (!WP_IS_SETTINGS (g_object))
    return FALSE;

  return g_str_equal (((WpSettings *) g_object)->metadata_name,
      (gchar *) metadata_name);
}

/*!
 * \brief Finds a registered WpSettings object by its metadata name
 *
 * \ingroup wpsettings
 * \param core the WpCore
 * \param metadata_name (nullable): the name of the metadata object that the
 *    settings object is associated with; NULL means the default "sm-settings"
 * \returns (transfer full) (nullable): the WpSettings object, or NULL if not
 *    found
 */
WpSettings *
wp_settings_find (WpCore * core, const gchar * metadata_name)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  GObject *s = wp_core_find_object (core, (GEqualFunc) find_settings_func,
      metadata_name ? metadata_name : "sm-settings");
  return s ? WP_SETTINGS (s) : NULL;
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
 * \brief Gets the WpSpaJson value of a setting
 * \ingroup wpsettings
 * \param self the settings object
 * \param name the name of the setting
 * \returns (transfer full) (nullable): The WpSpaJson value of the setting, or
 * NULL if the setting does not exist
 */
WpSpaJson *
wp_settings_get (WpSettings *self, const gchar *name)
{
  const gchar *value;
  g_autoptr (WpSettingsSpec) spec = NULL;
  g_autoptr (WpSpaJson) def_value = NULL;
  g_autoptr (WpMetadata) m = NULL;

  g_return_val_if_fail (WP_IS_SETTINGS (self), NULL);
  g_return_val_if_fail (name, NULL);

  spec = wp_settings_get_spec (self, name);
  if (!spec)
    return NULL;

  m = g_weak_ref_get (&self->metadata);
  if (!m)
    return wp_settings_spec_get_default_value (spec);

  value = wp_metadata_find (m, 0, name, NULL);
  return value ? wp_spa_json_new_wrap_string (value) :
      wp_settings_spec_get_default_value (spec);
}

/*!
 * \brief Gets the WpSpaJson saved value of a setting
 * \ingroup wpsettings
 * \param self the settings object
 * \param name the name of the setting
 * \returns (transfer full) (nullable): The WpSpaJson saved value of the
 * setting, or NULL if the setting does not exist
 */
WpSpaJson *
wp_settings_get_saved (WpSettings *self, const gchar *name)
{
  const gchar *value;
  g_autoptr (WpMetadata) mp = NULL;

  g_return_val_if_fail (WP_IS_SETTINGS (self), NULL);
  g_return_val_if_fail (name, NULL);

  mp = g_weak_ref_get (&self->metadata_persistent);
  if (!mp)
    return NULL;

  value = wp_metadata_find (mp, 0, name, NULL);
  return value ? wp_spa_json_new_wrap_string (value) : NULL;
}

/*!
 * \brief Gets the specification of a setting
 * \ingroup wpsettings
 * \param self the settings object
 * \param name the name of the setting
 * \returns (transfer full) (nullable): the specification of the setting
 */
WpSettingsSpec *
wp_settings_get_spec (WpSettings *self, const gchar *name)
{
  WpSettingsSpec *spec;

  g_return_val_if_fail (WP_IS_SETTINGS (self), NULL);
  g_return_val_if_fail (name, NULL);

  spec = g_hash_table_lookup (self->schema, name);
  return spec ? wp_settings_spec_ref (spec) : NULL;
}

/*!
 * \brief Sets a new setting value
 * \ingroup wpsettings
 * \param self the settings object
 * \param name the name of the setting
 * \param value (transfer none): the JSON value of the setting
 * \returns TRUE if the setting could be set, FALSE otherwise
 */
gboolean
wp_settings_set (WpSettings *self, const gchar *name, WpSpaJson *value)
{
  g_autoptr (WpMetadata) m = NULL;
  g_autoptr (WpSettingsSpec) spec = NULL;
  g_autofree gchar *value_str = NULL;

  g_return_val_if_fail (WP_IS_SETTINGS (self), FALSE);
  g_return_val_if_fail (name, FALSE);
  g_return_val_if_fail (value, FALSE);

  m = g_weak_ref_get (&self->metadata);
  if (!m)
    return FALSE;

  spec = wp_settings_get_spec (self, name);
  if (!spec)
    return FALSE;

  if (!wp_settings_spec_check_value (spec, value))
    return FALSE;

  value_str = wp_spa_json_to_string (value);
  wp_metadata_set (m, 0, name, "Spa:String:JSON", value_str);
  return TRUE;
}

/*!
 * \brief Resets the setting to its default value
 * \ingroup wpsettings
 * \param self the settings object
 * \param name the name of the setting to reset
 * \returns TRUE if the setting could be reset, FALSE otherwise
 */
gboolean
wp_settings_reset (WpSettings *self, const char *name)
{
  g_autoptr (WpSettingsSpec) spec = NULL;
  g_autoptr (WpSpaJson) def_value = NULL;

  g_return_val_if_fail (WP_IS_SETTINGS (self), FALSE);
  g_return_val_if_fail (name, FALSE);

  spec = wp_settings_get_spec (self, name);
  if (!spec)
    return FALSE;

  def_value = wp_settings_spec_get_default_value (spec);
  return wp_settings_set (self, name, def_value);
}

/*!
 * \brief Saves a setting to make it persistent after reboot
 * \ingroup wpsettings
 * \param self the settings object
 * \param name the name of the setting to be saved
 * \returns TRUE if the setting could be saved, FALSE otherwise
 */
gboolean
wp_settings_save (WpSettings *self, const char *name)
{
  g_autoptr (WpMetadata) mp = NULL;
  g_autoptr (WpSpaJson) value = NULL;
  g_autofree gchar *value_str = NULL;

  g_return_val_if_fail (WP_IS_SETTINGS (self), FALSE);
  g_return_val_if_fail (name, FALSE);

  mp = g_weak_ref_get (&self->metadata_persistent);
  if (!mp)
    return FALSE;

  value = wp_settings_get (self, name);
  if (!value)
    return FALSE;

  value_str = wp_spa_json_to_string (value);
  wp_metadata_set (mp, 0, name, "Spa:String:JSON", value_str);
  return TRUE;
}

/*!
 * \brief Deletes a saved setting to not make it persistent after reboot
 * \ingroup wpsettings
 * \param self the settings object
 * \param name the name of the saved setting to be deleted
 * \returns TRUE if the setting could be deleted, FALSE otherwise
 */
gboolean
wp_settings_delete (WpSettings *self, const char *name)
{
  g_autoptr (WpMetadata) mp = NULL;

  g_return_val_if_fail (WP_IS_SETTINGS (self), FALSE);
  g_return_val_if_fail (name, FALSE);

  mp = g_weak_ref_get (&self->metadata_persistent);
  if (!mp)
    return FALSE;

  wp_metadata_set (mp, 0, name, NULL, NULL);
  return TRUE;
}

/*!
 * \brief Resets all the settings to their default value
 * \ingroup wpsettings
 * \param self the settings object
 */
void wp_settings_reset_all (WpSettings *self)
{
  g_autoptr (WpMetadata) m = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  g_autoptr (WpProperties) props = NULL;

  g_return_if_fail (WP_IS_SETTINGS (self));

  m = g_weak_ref_get (&self->metadata);
  if (!m)
    return;

  /* We cannot reset the settings while iterating, as the current iterator
   * won't be valid anyore. Instead, we get a list of all settings, and then
   * we reset them */
  props = wp_properties_new_empty ();
  it = wp_metadata_new_iterator (m, 0);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpMetadataItem *mi = g_value_get_boxed (&item);
    const gchar *key = wp_metadata_item_get_key (mi);
    const gchar *value = wp_metadata_item_get_value (mi);
    wp_properties_set (props, key, value);
  }
  wp_iterator_unref (it);

  /* Now reset all settings */
  it = wp_properties_new_iterator (props);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);
    const gchar *key = wp_properties_item_get_key (pi);
    if (!wp_settings_reset (self, key))
      wp_warning_object (self, "Failed to reset setting %s", key);
  }
}

/*!
 * \brief Saves all the settings to make them persistent after reboot
 * \ingroup wpsettings
 * \param self the settings object
 */
void wp_settings_save_all (WpSettings *self)
{
  g_autoptr (WpMetadata) m = NULL;
  g_autoptr (WpMetadata) mp = NULL;
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  g_return_if_fail (WP_IS_SETTINGS (self));

  m = g_weak_ref_get (&self->metadata);
  mp = g_weak_ref_get (&self->metadata_persistent);
  if (!m || !mp)
    return;

  it = wp_metadata_new_iterator (m, 0);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpMetadataItem *mi = g_value_get_boxed (&item);
    const gchar *key = wp_metadata_item_get_key (mi);
    if (!wp_settings_save (self, key))
      wp_warning_object (self, "Failed to save setting %s", key);
  }
}

/*!
 * \brief Deletes all saved setting to not make them persistent after reboot
 * \ingroup wpsettings
 * \param self the settings object
 */
void wp_settings_delete_all (WpSettings *self)
{
  g_autoptr (WpMetadata) mp = NULL;

  g_return_if_fail (WP_IS_SETTINGS (self));

  mp = g_weak_ref_get (&self->metadata_persistent);
  if (!mp)
    return;

  wp_metadata_clear (mp);
}

struct settings_iterator_data
{
  WpSettings *settings;
  WpIterator *metadata_it;
};

static void
settings_iterator_reset (WpIterator *it)
{
  struct settings_iterator_data *it_data = wp_iterator_get_user_data (it);
  g_autoptr (WpMetadata) m = NULL;

  m = g_weak_ref_get (&it_data->settings->metadata);
  g_return_if_fail (m);

  g_clear_pointer (&it_data->metadata_it, wp_iterator_unref);
  it_data->metadata_it = wp_metadata_new_iterator (m, 0);
}

static gboolean
settings_iterator_next (WpIterator *it, GValue *item)
{
  struct settings_iterator_data *it_data = wp_iterator_get_user_data (it);
  g_autoptr (WpMetadata) m = NULL;
  g_autoptr (WpSettingsItem) si = NULL;
  g_auto (GValue) val = G_VALUE_INIT;
  WpMetadataItem *mi;
  const gchar *key, *value;

  m = g_weak_ref_get (&it_data->settings->metadata);
  g_return_val_if_fail (m, FALSE);

  if (!wp_iterator_next (it_data->metadata_it, &val))
    return FALSE;

  mi = g_value_get_boxed (&val);
  key = wp_metadata_item_get_key (mi);
  value = wp_metadata_item_get_value (mi);

  si = wp_settings_item_new (m, key, value);
  g_value_init (item, WP_TYPE_SETTINGS_ITEM);
  g_value_take_boxed (item, g_steal_pointer (&si));
  return TRUE;
}

static void
settings_iterator_finalize (WpIterator *it)
{
  struct settings_iterator_data *it_data = wp_iterator_get_user_data (it);
  g_clear_pointer (&it_data->metadata_it, wp_iterator_unref);
  g_clear_object (&it_data->settings);
}

static const WpIteratorMethods settings_iterator_methods = {
  .version = WP_ITERATOR_METHODS_VERSION,
  .reset = settings_iterator_reset,
  .next = settings_iterator_next,
  .fold = NULL,
  .foreach = NULL,
  .finalize = settings_iterator_finalize,
};

/*!
 * \brief Iterates over settings
 * \ingroup wpsettings
 * \param self the settings object
 * \returns (transfer full): an iterator that iterates over the settings.
 */
WpIterator *
wp_settings_new_iterator (WpSettings *self)
{
  g_autoptr (WpIterator) it = NULL;
  struct settings_iterator_data *it_data;
  g_autoptr (WpMetadata) m = NULL;

  g_return_val_if_fail (WP_IS_SETTINGS (self), NULL);

  m = g_weak_ref_get (&self->metadata);
  if (!m)
    return NULL;

  it = wp_iterator_new (&settings_iterator_methods,
      sizeof (struct settings_iterator_data));
  it_data = wp_iterator_get_user_data (it);
  it_data->settings = g_object_ref (self);
  it_data->metadata_it = wp_metadata_new_iterator (m, 0);
  return g_steal_pointer (&it);
}
