/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: WpState
 *
 * The #WpState class saves and loads properties from a file
 */

#define G_LOG_DOMAIN "wp-state"

#define WP_STATE_DIR_NAME "wireplumber"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "debug.h"
#include "state.h"

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

static gboolean
path_exists (const char *path)
{
  struct stat info;
  return stat (path, &info) == 0;
}

static char *
get_new_location (const char *name)
{
  g_autofree gchar *path = NULL;

  /* Get the config path */
  path = g_build_filename (g_get_user_config_dir (), WP_STATE_DIR_NAME, NULL);
  g_return_val_if_fail (path, NULL);

  /* Create the directory if it doesn't exist */
  if (!path_exists (path))
    g_mkdir_with_parents (path, 0700);

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
  g_clear_pointer (&self->keyfile, g_key_file_free);

  G_OBJECT_CLASS (wp_state_parent_class)->finalize (object);
}

static void
wp_state_init (WpState * self)
{
  self->keyfile = g_key_file_new ();
}

static void
wp_state_class_init (WpStateClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_state_finalize;
  object_class->set_property = wp_state_set_property;
  object_class->get_property = wp_state_get_property;

  /**
   * WpState:name:
   * The file name where the state will be stored.
   */
  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name",
          "The file name where the state will be stored", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_state_new:
 * @name: the state name
 *
 * Returns: (transfer full): the new #WpState
 */
WpState *
wp_state_new (const gchar *name)
{
  g_return_val_if_fail (name, NULL);
  return g_object_new (wp_state_get_type (),
      "name", name,
      NULL);
}

/**
 * wp_state_get_name:
 * @self: the state
 *
 * Returns: the name of this state
 */
const gchar *
wp_state_get_name (WpState *self)
{
  g_return_val_if_fail (WP_IS_STATE (self), NULL);

  return self->name;
}

/**
 * wp_state_get_location:
 * @self: the state
 *
 * Returns: the location of this state
 */
const gchar *
wp_state_get_location (WpState *self)
{
  g_return_val_if_fail (WP_IS_STATE (self), NULL);
  wp_state_ensure_location (self);

  return self->location;
}

/**
 * wp_state_clear:
 * @self: the state
 *
 * Clears the state removing its file
 */
void
wp_state_clear (WpState *self)
{
  g_return_if_fail (WP_IS_STATE (self));
  wp_state_ensure_location (self);

  if (path_exists (self->location))
    remove (self->location);
}

/**
 * wp_state_save:
 * @self: the state
 * @group: the group name where the properties will be save
 * @props: (transfer none): the properties to save
 *
 * Saves new properties in the state, overwriting all previous data.
 *
 * Returns: TRUE if the properties could be saved, FALSE otherwise
 */
gboolean
wp_state_save (WpState *self, const gchar *group, WpProperties *props)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  g_return_val_if_fail (WP_IS_STATE (self), FALSE);
  g_return_val_if_fail (group, FALSE);
  wp_state_ensure_location (self);

  wp_info_object (self, "saving state into %s", self->location);

  g_key_file_remove_group (self->keyfile, group, NULL);

  /* Set the properties */
  for (it = wp_properties_iterate (props);
      wp_iterator_next (it, &item);
      g_value_unset (&item)) {
    const gchar *key = wp_properties_iterator_item_get_key (&item);
    const gchar *val = wp_properties_iterator_item_get_value (&item);
    g_key_file_set_string (self->keyfile, group, key, val);
  }

  if (!g_key_file_save_to_file (self->keyfile, self->location, NULL)) {
    wp_critical_object (self, "can't save %s", self->location);
    return FALSE;
  }

  return TRUE;
}

/**
 * wp_state_load:
 * @self: the state
 * @group: the group which the properties will be loaded from
 *
 * Loads the state data into new properties.
 *
 * Returns (transfer full): the new properties with the state data
 */
WpProperties *
wp_state_load (WpState *self, const gchar *group)
{
  g_autoptr (WpProperties) props = wp_properties_new_empty ();
  gchar ** keys = NULL;

  g_return_val_if_fail (WP_IS_STATE (self), NULL);
  g_return_val_if_fail (group, NULL);
  wp_state_ensure_location (self);

  /* Open */
  if (!g_key_file_load_from_file (self->keyfile, self->location,
      G_KEY_FILE_NONE, NULL))
    return g_steal_pointer (&props);

  /* Load all keys */
  keys = g_key_file_get_keys (self->keyfile, group, NULL, NULL);
  if (!keys)
    return g_steal_pointer (&props);

  for (guint i = 0; keys[i]; i++) {
    const gchar *key = keys[i];
    g_autofree gchar *val = NULL;
    val = g_key_file_get_string (self->keyfile, group, key, NULL);
    if (!val)
      continue;
    wp_properties_set (props, key, val);
  }

  g_strfreev (keys);

  return g_steal_pointer (&props);
}
