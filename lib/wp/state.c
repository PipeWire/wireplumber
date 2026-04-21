/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <errno.h>

#include "log.h"
#include "state.h"
#include "wp.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-state")

#define DEFAULT_TIMEOUT_MS 1000
#define ESCAPED_CHARACTER '\\'

static char *
escape_string (const gchar *str)
{
  char *res = NULL;
  size_t str_size, i, j;

  g_return_val_if_fail (str, NULL);
  str_size = strlen (str);
  g_return_val_if_fail (str_size > 0, NULL);

  res = g_malloc_n ((str_size * 2) + 1, sizeof(gchar));

  j = 0;
  for (i = 0; i < str_size; i++) {
    switch (str[i]) {
      case ESCAPED_CHARACTER:
        res[j++] = ESCAPED_CHARACTER;
        res[j++] = ESCAPED_CHARACTER;
        break;
      case ' ':
        res[j++] = ESCAPED_CHARACTER;
        res[j++] = 's';
        break;
      case '=':
        res[j++] = ESCAPED_CHARACTER;
        res[j++] = 'e';
        break;
      case '[':
        res[j++] = ESCAPED_CHARACTER;
        res[j++] = 'o';
        break;
      case ']':
        res[j++] = ESCAPED_CHARACTER;
        res[j++] = 'c';
        break;
      default:
        res[j++] = str[i];
        break;
    }
  }
  res[j++] = '\0';

  return res;
}

static char *
compress_string (const gchar *str)
{
  char *res = NULL;
  size_t str_size, i, j;

  g_return_val_if_fail (str, NULL);
  str_size = strlen (str);
  g_return_val_if_fail (str_size > 0, NULL);

  res = g_malloc_n (str_size + 1, sizeof(gchar));

  j = 0;
  for (i = 0; i < str_size - 1; i++) {
    if (str[i] == ESCAPED_CHARACTER) {
      switch (str[i + 1]) {
        case ESCAPED_CHARACTER:
          res[j++] = ESCAPED_CHARACTER;
          break;
        case 's':
          res[j++] = ' ';
          break;
        case 'e':
          res[j++] = '=';
          break;
        case 'o':
          res[j++] = '[';
          break;
        case 'c':
          res[j++] = ']';
          break;
        default:
          res[j++] = str[i];
          break;
      }
      i++;
    } else {
      res[j++] = str[i];
    }
  }
  if (i < str_size)
    res[j++] = str[i];
  res[j++] = '\0';

  return res;
}

/*! \defgroup wpstate WpState */
/*!
 * \struct WpState
 *
 * The WpState class saves and loads properties from a file
 *
 * \gproperties
 * \gproperty{name, gchar *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The file name where the state will be stored.}
 */

enum {
  PROP_0,
  PROP_NAME,
  PROP_TIMEOUT,
};

struct _WpState
{
  GObject parent;

  /* Props */
  gchar *name;
  guint timeout;

  gchar *location;
  GSource *timeout_source;
  WpProperties *timeout_props;
};

G_DEFINE_TYPE (WpState, wp_state, G_TYPE_OBJECT)

/* Gets the full path to the WirePlumber XDG_STATE_HOME subdirectory */
static const gchar *
wp_get_xdg_state_dir (void)
{
  static gchar xdg_dir[PATH_MAX] = {0};
  if (xdg_dir[0] == '\0') {
    g_autofree gchar *path = NULL;
    g_autofree gchar *base = g_strdup (g_getenv ("XDG_STATE_HOME"));
    if (!base)
      base = g_build_filename (g_get_home_dir (), ".local", "state", NULL);

    path = g_build_filename (base, "wireplumber", NULL);
    (void) g_strlcpy (xdg_dir, path, sizeof (xdg_dir));
  }
  return xdg_dir;
}

static char *
get_new_location (const char *name)
{
  const gchar *path = wp_get_xdg_state_dir ();

  /* Create the directory if it doesn't exist */
  if (g_mkdir_with_parents (path, 0700) < 0)
    wp_warning ("failed to create directory %s: %s", path, g_strerror (errno));

  return g_build_filename (path, name, NULL);
}

static void
wp_state_ensure_location (WpState *self)
{
  if (!self->location)
    self->location = get_new_location (self->name);
  g_return_if_fail (self->location);
}

static void
wp_state_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpState *self = WP_STATE (object);

  switch (property_id) {
  case PROP_NAME:
    g_clear_pointer (&self->name, g_free);
    self->name = g_value_dup_string (value);
    break;
  case PROP_TIMEOUT:
    self->timeout = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_state_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpState *self = WP_STATE (object);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  case PROP_TIMEOUT:
    g_value_set_uint (value, self->timeout);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_state_finalize (GObject * object)
{
  WpState * self = WP_STATE (object);

  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->location, g_free);
  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->timeout_props, wp_properties_unref);

  G_OBJECT_CLASS (wp_state_parent_class)->finalize (object);
}

static void
wp_state_init (WpState * self)
{
  self->timeout = DEFAULT_TIMEOUT_MS;
}

static void
wp_state_class_init (WpStateClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_state_finalize;
  object_class->set_property = wp_state_set_property;
  object_class->get_property = wp_state_get_property;

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name",
          "The file name where the state will be stored", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "timeout",
          "The timeout in milliseconds to save the state", 0, G_MAXUINT,
          DEFAULT_TIMEOUT_MS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Constructs a new state object
 * \ingroup wpstate
 * \param name the state name
 * \returns (transfer full): the new WpState
 */
WpState *
wp_state_new (const gchar *name)
{
  g_return_val_if_fail (name, NULL);
  return g_object_new (wp_state_get_type (),
      "name", name,
      NULL);
}

/*!
 * \brief Gets the name of a state object
 * \ingroup wpstate
 * \param self the state
 * \returns the name of this state
 */
const gchar *
wp_state_get_name (WpState *self)
{
  g_return_val_if_fail (WP_IS_STATE (self), NULL);

  return self->name;
}

/*!
 * \brief Gets the location of a state object
 * \ingroup wpstate
 * \param self the state
 * \returns the location of this state
 */
const gchar *
wp_state_get_location (WpState *self)
{
  g_return_val_if_fail (WP_IS_STATE (self), NULL);
  wp_state_ensure_location (self);

  return self->location;
}

/*!
 * \brief Clears the state removing its file
 * \ingroup wpstate
 * \param self the state
 */
void
wp_state_clear (WpState *self)
{
  g_return_if_fail (WP_IS_STATE (self));
  wp_state_ensure_location (self);
  if (remove (self->location) < 0)
    wp_warning ("failed to remove %s: %s", self->location, g_strerror (errno));
}

static gboolean
wp_state_save_internal (const gchar *name, const gchar *location,
    WpProperties *props, GError ** error)
{
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  GError *err = NULL;

  g_return_val_if_fail (name, FALSE);
  g_return_val_if_fail (location, FALSE);
  g_return_val_if_fail (props, FALSE);

  /* Set the properties */
  for (it = wp_properties_new_iterator (props);
      wp_iterator_next (it, &item);
      g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);
    const gchar *key = wp_properties_item_get_key (pi);
    const gchar *val = wp_properties_item_get_value (pi);
    g_autofree gchar *escaped_key = escape_string (key);
    if (escaped_key)
      g_key_file_set_string (keyfile, name, escaped_key, val);
  }

  if (!g_key_file_save_to_file (keyfile, location, &err)) {
    g_propagate_prefixed_error (error, err, "could not save %s: ", name);
    return FALSE;
  }

  return TRUE;
}

/*!
 * \brief Saves new properties in the state, overwriting all previous data.
 * \ingroup wpstate
 * \param self the state
 * \param props (transfer none): the properties to save
 * \param error (out)(optional): return location for a GError, or NULL
 * \returns TRUE if the properties could be saved, FALSE otherwise
 */
gboolean
wp_state_save (WpState *self, WpProperties *props, GError ** error)
{
  g_return_val_if_fail (WP_IS_STATE (self), FALSE);

  wp_state_ensure_location (self);
  return wp_state_save_internal (self->name, self->location, props, error);
}

static gboolean
timeout_save_state_callback (WpState *self)
{
  g_autoptr (GError) error = NULL;

  if (!wp_state_save (self, self->timeout_props, &error))
    wp_warning_object (self, "%s", error->message);

  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->timeout_props, wp_properties_unref);

  return G_SOURCE_REMOVE;
}

/*!
 * \brief Saves new properties in the state, overwriting all previous data,
 *   after a timeout
 *
 * This is similar to wp_state_save() but it will save the state after a timeout
 * has elapsed. If the state is saved again before the timeout elapses, the
 * timeout is reset.
 *
 * This function is useful to avoid saving the state too often. When called
 * consecutively, it will save the state only once. Every time it is called,
 * it will cancel the previous timer and start a new one, resulting in timing
 * out only after the last call.
 *
 * \ingroup wpstate
 * \param self the state
 * \param core the core, used to add the timeout callback to the main loop
 * \param props (transfer none): the properties to save. This object will be
 *   referenced and kept alive until the timeout elapses, but not deep copied.
 * \since 0.5.0
 */
void
wp_state_save_after_timeout (WpState *self, WpCore *core, WpProperties *props)
{
  /* Clear the current timeout callback */
  if (self->timeout_source)
    g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);
  g_clear_pointer (&self->timeout_props, wp_properties_unref);

  self->timeout_props = wp_properties_ref (props);

  /* Add the timeout callback */
  wp_core_timeout_add_closure (core, &self->timeout_source, self->timeout,
      g_cclosure_new_object (G_CALLBACK (timeout_save_state_callback),
          G_OBJECT (self)));
}

static WpProperties *
wp_state_load_internal (const gchar *name, const gchar *location)
{
  g_autoptr (GKeyFile) keyfile = NULL;
  g_autoptr (WpProperties) props = NULL;
  gchar ** keys = NULL;

  g_return_val_if_fail (name, NULL);
  g_return_val_if_fail (location, NULL);

  props = wp_properties_new_empty ();

  /* Open */
  keyfile = g_key_file_new ();
  if (!g_key_file_load_from_file (keyfile, location,
      G_KEY_FILE_NONE, NULL))
    return g_steal_pointer (&props);

  /* Load all keys */
  keys = g_key_file_get_keys (keyfile, name, NULL, NULL);
  if (!keys)
    return g_steal_pointer (&props);

  for (guint i = 0; keys[i]; i++) {
    g_autofree gchar *compressed_key = NULL;
    const gchar *key = keys[i];
    g_autofree gchar *val = NULL;
    val = g_key_file_get_string (keyfile, name, key, NULL);
    if (!val)
      continue;
    compressed_key = compress_string (key);
    if (compressed_key)
      wp_properties_set (props, compressed_key, val);
  }

  g_strfreev (keys);

  return g_steal_pointer (&props);
}

/*!
 * \brief Loads the state data from the file system
 *
 * This function will never fail. If it cannot load the state, for any reason,
 * it will simply return an empty WpProperties, behaving as if there was no
 * previous state stored.
 *
 * \ingroup wpstate
 * \param self the state
 * \returns (transfer full): a new WpProperties containing the state data
 */
WpProperties *
wp_state_load (WpState *self)
{
  g_return_val_if_fail (WP_IS_STATE (self), NULL);

  wp_state_ensure_location (self);
  return wp_state_load_internal (self->name, self->location);
}


/* WpStateMetadata */

/*! \defgroup wpstatemetadata WpStateMetadata */
/*!
 * \struct WpStateMetadata
 *
 * The WpStateMetadata class saves and loads properties from a file and reflects
 * the state in a metadata object.
 *
 * \gproperties
 * \gproperty{name, gchar *, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY,
 *   The file name where the state will be stored.}
 */

enum {
  STATE_METADATA_PROP_0,
  STATE_METADATA_PROP_NAME,
  STATE_METADATA_PROP_TIMEOUT,
};

struct _WpStateMetadata
{
  WpObject parent;

  /* Props */
  gchar *name;
  guint timeout;

  gchar *location;
  WpProperties *metadata_props;
  WpImplMetadata *metadata;
  GSource *timeout_source;
};

G_DEFINE_TYPE (WpStateMetadata, wp_state_metadata, WP_TYPE_OBJECT)

static void
wp_state_metadata_init (WpStateMetadata * self)
{
  self->timeout = DEFAULT_TIMEOUT_MS;
}

static void
wp_state_metadata_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpStateMetadata *self = WP_STATE_METADATA (object);

  switch (property_id) {
  case STATE_METADATA_PROP_NAME:
    g_clear_pointer (&self->name, g_free);
    self->name = g_value_dup_string (value);
    break;
  case STATE_METADATA_PROP_TIMEOUT:
    self->timeout = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_state_metadata_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpStateMetadata *self = WP_STATE_METADATA (object);

  switch (property_id) {
  case STATE_METADATA_PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  case STATE_METADATA_PROP_TIMEOUT:
    g_value_set_uint (value, self->timeout);
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
wp_state_metadata_get_supported_features (WpObject * self)
{
  return WP_STATE_METADATA_LOADED;
}

static guint
wp_state_metadata_activate_get_next_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_STATE_METADATA_LOADED,
      WP_TRANSITION_STEP_ERROR);

  return STEP_LOAD;
}

static void
state_metadata_ensure_location (WpStateMetadata *self)
{
  if (!self->location)
    self->location = get_new_location (self->name);
  g_return_if_fail (self->location);
}

static WpProperties *
state_metadata_load (WpStateMetadata *self)
{
  state_metadata_ensure_location (self);
  return wp_state_load_internal (self->name, self->location);
}

static gboolean
state_metadata_save (WpStateMetadata *self, WpProperties *props,
    GError ** error)
{
  state_metadata_ensure_location (self);
  return wp_state_save_internal (self->name, self->location, props, error);
}

static gboolean
state_metadata_timeout_save_cb (WpStateMetadata *self)
{
  g_autoptr (GError) error = NULL;

  if (!state_metadata_save (self, self->metadata_props, &error))
    wp_warning_object (self, "%s", error->message);

  g_clear_pointer (&self->timeout_source, g_source_unref);

  wp_info_object (self, "saved changes on state metadata '%s'", self->name);
  return G_SOURCE_REMOVE;
}

static void
state_metadata_save_after_timeout (WpStateMetadata *self)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));
  g_return_if_fail (core);

  /* Clear the current timeout callback */
  if (self->timeout_source)
    g_source_destroy (self->timeout_source);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  /* Add the timeout callback */
  wp_core_timeout_add_closure (core, &self->timeout_source, self->timeout,
      g_cclosure_new_object (G_CALLBACK (state_metadata_timeout_save_cb),
          G_OBJECT (self)));
}

static void
state_metadata_clear (WpStateMetadata *self)
{
  if (self->metadata_props)
    wp_properties_clear (self->metadata_props);

  state_metadata_ensure_location (self);
  if (remove (self->location) < 0)
    wp_warning ("failed to remove %s: %s", self->location, g_strerror (errno));
}

static void
on_state_metadata_changed (WpMetadata *m, guint32 subject,
    const gchar *key, const gchar *type, const gchar *value, gpointer d)
{
  WpStateMetadata * self = WP_STATE_METADATA (d);

  /* Save new key after timeout if valid. Otherwise just clear the state */
  if (key) {
    wp_properties_set (self->metadata_props, key, value);
    if (value)
      wp_info_object (self, "key changed on state metadata '%s': %s = %s",
          self->name, key, value);
    else
      wp_info_object (self, "key removed on state metadata '%s': %s",
          self->name, key);
    state_metadata_save_after_timeout (self);
  } else {
    state_metadata_clear (self);
    wp_info_object (self, "cleared state metadata '%s'", self->name);
  }
}

static void
on_metadata_activated (WpObject * proxy, GAsyncResult * res,
    WpTransition * transition)
{
  WpStateMetadata *self = wp_transition_get_source_object (transition);
  g_autoptr (GError) error = NULL;
  g_autoptr (WpIterator) it = NULL;
  GValue v = G_VALUE_INIT;

  /* Make sure there were no errors when activating the metadata */
  if (!wp_object_activate_finish (proxy, res, &error)) {
    wp_transition_return_error (transition, g_error_new (
        WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Failed to activate metadata for state %s: %s", self->name,
        error->message));
    return;
  }

  /* Load the state keys into the metadata */
  g_return_if_fail (self->metadata_props == NULL);
  self->metadata_props = state_metadata_load (self);
  it = wp_properties_new_iterator (self->metadata_props);
  while (wp_iterator_next (it, &v)) {
    WpPropertiesItem *pi = g_value_get_boxed (&v);
    const gchar *key = wp_properties_item_get_key (pi);
    const gchar *value = wp_properties_item_get_value (pi);
    wp_metadata_set (WP_METADATA (self->metadata), 0, key, NULL, value);
    g_value_unset (&v);
  }

  /* Handle metadata changes */
  g_signal_connect_object (self->metadata, "changed",
      G_CALLBACK (on_state_metadata_changed), self, 0);

  wp_object_update_features (WP_OBJECT (self), WP_STATE_METADATA_LOADED, 0);
}

static void
wp_state_metadata_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpStateMetadata * self = WP_STATE_METADATA (object);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  switch (step) {
  case STEP_LOAD: {
    g_return_if_fail (self->metadata == NULL);
    self->metadata = wp_impl_metadata_new_full (core, self->name,
        wp_properties_new ("wireplumber.state", "true", NULL));
    wp_object_activate_closure (WP_OBJECT (self->metadata),
        WP_OBJECT_FEATURES_ALL, NULL, g_cclosure_new_object (
            (GCallback) on_metadata_activated, G_OBJECT (transition)));
    break;
  }
  case WP_TRANSITION_STEP_ERROR:
    break;
  default:
    g_assert_not_reached ();
  }
}

static void
wp_state_metadata_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpStateMetadata *self = WP_STATE_METADATA (object);

  if (self->metadata)
    g_signal_handlers_disconnect_by_data (self->metadata, self);

  g_clear_pointer (&self->metadata_props, wp_properties_unref);
  g_clear_object (&self->metadata);

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_state_metadata_finalize (GObject * object)
{
  WpStateMetadata * self = WP_STATE_METADATA (object);

  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->location, g_free);
  g_clear_pointer (&self->timeout_source, g_source_unref);

  G_OBJECT_CLASS (wp_state_metadata_parent_class)->finalize (object);
}

static void
wp_state_metadata_class_init (WpStateMetadataClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass * wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_state_metadata_finalize;
  object_class->set_property = wp_state_metadata_set_property;
  object_class->get_property = wp_state_metadata_get_property;

  wpobject_class->get_supported_features =
      wp_state_metadata_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_state_metadata_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_state_metadata_activate_execute_step;
  wpobject_class->deactivate = wp_state_metadata_deactivate;

  g_object_class_install_property (object_class, STATE_METADATA_PROP_NAME,
      g_param_spec_string ("name", "name",
          "The file name where the state metadata will be stored", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, STATE_METADATA_PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "timeout",
          "The timeout in milliseconds to save the state metadata", 0,
          G_MAXUINT, DEFAULT_TIMEOUT_MS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Constructs a new state metadata object
 * \ingroup wpstatemetadata
 * \param core the core associated with the state metadata
 * \param name the state metadata name
 * \returns (transfer full): the new WpStateMetadata
 */
WpStateMetadata *
wp_state_metadata_new (WpCore *core, const gchar *name)
{
  g_return_val_if_fail (core, NULL);
  g_return_val_if_fail (name, NULL);
  return g_object_new (wp_state_metadata_get_type (),
      "core", core,
      "name", name,
      NULL);
}

/*!
 * \brief Gets the name of a state metadata object
 * \ingroup wpstatemetadata
 * \param self the state metadata
 * \returns the name of this state metadata
 */
const gchar *
wp_state_metadata_get_name (WpStateMetadata *self)
{
  g_return_val_if_fail (WP_IS_STATE_METADATA (self), NULL);

  return self->name;
}

/*!
 * \brief Gets the location of a state metadata object
 * \ingroup wpstatemetadata
 * \param self the state metadata
 * \returns the location of this state metadata
 */
const gchar *
wp_state_metadata_get_location (WpStateMetadata *self)
{
  g_return_val_if_fail (WP_IS_STATE_METADATA (self), NULL);

  state_metadata_ensure_location (self);

  return self->location;
}

/*!
 * \brief Clears the state metadata and removes its file
 *
 * If the state metadata has not been loaded, this won't do anything.
 *
 * \ingroup wpstatemetadata
 * \param self the state metadata
 */
void
wp_state_metadata_clear (WpStateMetadata *self)
{
  g_return_if_fail (WP_IS_STATE_METADATA (self));

  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
      WP_STATE_METADATA_LOADED))
    return;

  g_return_if_fail (self->metadata);
  wp_metadata_clear (WP_METADATA (self->metadata));
}

/*!
 * \brief Gets a value from the state metadata
 *
 * If the state metadata has not been loaded, this won't do anything.
 *
 * \ingroup wpstatemetadata
 * \param self the state metadata
 * \param key the key of the value
 * \returns the value from the state metadata, or NULL if not found.
 */
const gchar *
wp_state_metadata_get (WpStateMetadata *self, const gchar *key)
{
  g_return_val_if_fail (WP_IS_STATE_METADATA (self), NULL);
  g_return_val_if_fail (key, NULL);

  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
      WP_STATE_METADATA_LOADED))
    return NULL;

  g_return_val_if_fail (self->metadata, NULL);
  return wp_metadata_find (WP_METADATA (self->metadata), 0, key, NULL);
}

/*!
 * \brief Sets a value into the state metadata
 *
 * If value is NULL, it will unset the given \a key. Note that this will also
 * save the state after the timeout has elapsed.
 *
 * If the state metadata has not been loaded, this won't do anything.
 *
 * \ingroup wpstatemetadata
 * \param self the metadata object
 * \param key: the key to set.
 * \param value (nullable): the value to set, or NULL to unset the given \a key
 */
void
wp_state_metadata_set (WpStateMetadata *self, const gchar *key,
    const gchar *value)
{
  g_return_if_fail (WP_IS_STATE_METADATA (self));
  g_return_if_fail (key);

  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
      WP_STATE_METADATA_LOADED))
    return;

  g_return_if_fail (self->metadata);
  wp_metadata_set (WP_METADATA (self->metadata), 0, key, NULL, value);
}
