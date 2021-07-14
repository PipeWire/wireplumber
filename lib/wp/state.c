/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-state"

#include <stdio.h>
#include <errno.h>

#include "log.h"
#include "state.h"
#include "wp.h"

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
};

struct _WpState
{
  GObject parent;

  /* Props */
  gchar *name;

  gchar *location;
  GKeyFile *keyfile;
};

G_DEFINE_TYPE (WpState, wp_state, G_TYPE_OBJECT)

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

  G_OBJECT_CLASS (wp_state_parent_class)->finalize (object);
}

static void
wp_state_init (WpState * self)
{
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
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  GError *err = NULL;

  g_return_val_if_fail (WP_IS_STATE (self), FALSE);
  g_return_val_if_fail (props, FALSE);
  wp_state_ensure_location (self);

  wp_info_object (self, "saving state into %s", self->location);

  /* Set the properties */
  for (it = wp_properties_new_iterator (props);
      wp_iterator_next (it, &item);
      g_value_unset (&item)) {
    WpPropertiesItem *pi = g_value_get_boxed (&item);
    const gchar *key = wp_properties_item_get_key (pi);
    const gchar *val = wp_properties_item_get_value (pi);
    g_autofree gchar *escaped_key = escape_string (key);
    if (escaped_key)
      g_key_file_set_string (keyfile, self->name, escaped_key, val);
  }

  if (!g_key_file_save_to_file (keyfile, self->location, &err)) {
    g_propagate_prefixed_error (error, err, "could not save %s: ", self->name);
    return FALSE;
  }

  return TRUE;
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
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();
  g_autoptr (WpProperties) props = wp_properties_new_empty ();
  gchar ** keys = NULL;

  g_return_val_if_fail (WP_IS_STATE (self), NULL);
  wp_state_ensure_location (self);

  /* Open */
  if (!g_key_file_load_from_file (keyfile, self->location,
      G_KEY_FILE_NONE, NULL))
    return g_steal_pointer (&props);

  /* Load all keys */
  keys = g_key_file_get_keys (keyfile, self->name, NULL, NULL);
  if (!keys)
    return g_steal_pointer (&props);

  for (guint i = 0; keys[i]; i++) {
    g_autofree gchar *compressed_key = NULL;
    const gchar *key = keys[i];
    g_autofree gchar *val = NULL;
    val = g_key_file_get_string (keyfile, self->name, key, NULL);
    if (!val)
      continue;
    compressed_key = compress_string (key);
    if (compressed_key)
      wp_properties_set (props, compressed_key, val);
  }

  g_strfreev (keys);

  return g_steal_pointer (&props);
}
