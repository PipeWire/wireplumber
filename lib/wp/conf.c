/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "conf.h"
#include "log.h"
#include "json-utils.h"
#include "base-dirs.h"
#include "error.h"

#include <pipewire/pipewire.h>
#include <spa/utils/result.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-conf")

#define OVERRIDE_SECTION_PREFIX "override."

/*! \defgroup wpconf WpConf */
/*!
 * \struct WpConf
 *
 * WpConf allows accessing the different sections of the wireplumber
 * configuration.
 */

typedef struct _WpConfSection WpConfSection;
struct _WpConfSection
{
  gchar *name;
  WpSpaJson *value;
  gchar *location;
};

static void
wp_conf_section_clear (WpConfSection * section)
{
  g_free (section->name);
  g_clear_pointer (&section->value, wp_spa_json_unref);
  g_free (section->location);
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (WpConfSection, wp_conf_section_clear)

struct _WpConf
{
  GObject parent;

  /* Props */
  gchar *name;
  WpProperties *properties;

  /* Private */
  GArray *conf_sections; /* element-type: WpConfSection */
  GPtrArray *files; /* element-type: GMappedFile* */
};

enum {
  PROP_0,
  PROP_NAME,
  PROP_PROPERTIES,
};

G_DEFINE_TYPE (WpConf, wp_conf, G_TYPE_OBJECT)

static void
wp_conf_init (WpConf * self)
{
  self->conf_sections = g_array_new (FALSE, FALSE, sizeof (WpConfSection));
  g_array_set_clear_func (self->conf_sections, (GDestroyNotify) wp_conf_section_clear);
  self->files = g_ptr_array_new_with_free_func ((GDestroyNotify) g_mapped_file_unref);
}

static void
wp_conf_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpConf *self = WP_CONF (object);

  switch (property_id) {
  case PROP_NAME:
    self->name = g_value_dup_string (value);
    break;
  case PROP_PROPERTIES:
    self->properties = g_value_dup_boxed (value);
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
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  case PROP_PROPERTIES:
    g_value_take_boxed (value, wp_properties_copy (self->properties));
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

  wp_conf_close (self);
  g_clear_pointer (&self->properties, wp_properties_unref);
  g_clear_pointer (&self->conf_sections, g_array_unref);
  g_clear_pointer (&self->files, g_ptr_array_unref);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (wp_conf_parent_class)->finalize (object);
}

static void
wp_conf_class_init (WpConfClass * klass)
{
  GObjectClass * object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = wp_conf_finalize;
  object_class->set_property = wp_conf_set_property;
  object_class->get_property = wp_conf_get_property;

  g_object_class_install_property(object_class, PROP_NAME,
        g_param_spec_string ("name", "name", "The name of the configuration file",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(object_class, PROP_PROPERTIES,
        g_param_spec_boxed ("properties", "properties", "WpProperties",
          WP_TYPE_PROPERTIES, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Creates a new WpConf object
 *
 * This does not open the files, it only creates the object. For most use cases,
 * you should use wp_conf_new_open() instead.
 *
 * \ingroup wpconf
 * \param name the name of the configuration file
 * \param properties (transfer full) (nullable): a WpProperties with keys
 *    specifying how to load the WpConf object
 * \returns (transfer full): a new WpConf object
 */
WpConf *
wp_conf_new (const gchar * name, WpProperties * properties)
{
  g_return_val_if_fail (name, NULL);
  g_autoptr (WpProperties) props = properties;
  return g_object_new (WP_TYPE_CONF, "name", name,
                       "properties", props,
                       NULL);
}

/*!
 * \brief Creates a new WpConf object and opens the configuration file and its
 *   fragments, keeping them mapped in memory for further access.
 *
 * \ingroup wpconf
 * \param name the name of the configuration file
 * \param properties (transfer full) (nullable): a WpProperties with keys
 *    specifying how to load the WpConf object
 * \param error (out) (nullable): return location for a GError, or NULL
 * \returns (transfer full) (nullable): a new WpConf object, or NULL
 *   if an error occurred
 */
WpConf *
wp_conf_new_open (const gchar * name, WpProperties * properties, GError ** error)
{
  g_return_val_if_fail (name, NULL);

  g_autoptr (WpConf) self = wp_conf_new (name, properties);
  if (!wp_conf_open (self, error))
    return NULL;
  return g_steal_pointer (&self);
}

static gboolean
detect_old_conf_format (WpConf * self, GMappedFile *file)
{
  const gchar *data = g_mapped_file_get_contents (file);
  gsize size = g_mapped_file_get_length (file);

  /* wireplumber 0.4 used to have components of type = config/lua */
  return g_strrstr_len (data, size, "config/lua") ? TRUE : FALSE;
}

static gboolean
open_and_load_sections (WpConf * self, const gchar *path, GError ** error)
{
  g_autoptr (GMappedFile) file = g_mapped_file_new (path, FALSE, error);
  if (!file)
    return FALSE;

  /* test if the file is a relic from 0.4 */
  if (detect_old_conf_format (self, file)) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
        "The configuration file at '%s' is likely an old WirePlumber 0.4 config "
        "and is not supported anymore. Try removing it.", path);
    return FALSE;
  }

  g_autoptr (WpSpaJson) json = wp_spa_json_new_wrap_stringn (
      g_mapped_file_get_contents (file), g_mapped_file_get_length (file));
  g_autoptr (WpSpaJsonParser) parser = wp_spa_json_parser_new_undefined (json);
  g_autoptr (GArray) sections = g_array_new (FALSE, FALSE, sizeof (WpConfSection));

  g_array_set_clear_func (sections, (GDestroyNotify) wp_conf_section_clear);

  while (TRUE) {
    g_auto (WpConfSection) section = { 0, };
    g_autoptr (WpSpaJson) tmp = NULL;

    /* parse the section name */
    tmp = wp_spa_json_parser_get_json (parser);
    if (!tmp)
      break;

    if (wp_spa_json_is_container (tmp) ||
        wp_spa_json_is_int (tmp) ||
        wp_spa_json_is_float (tmp) ||
        wp_spa_json_is_boolean (tmp) ||
        wp_spa_json_is_null (tmp))
    {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "invalid section name (not a string): %.*s",
          (int) wp_spa_json_get_size (tmp), wp_spa_json_get_data (tmp));
      return FALSE;
    }

    section.name = wp_spa_json_parse_string (tmp);
    g_clear_pointer (&tmp, wp_spa_json_unref);

    /* parse the section contents */
    tmp = wp_spa_json_parser_get_json (parser);
    if (!tmp) {
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "section '%s' has no value", section.name);
      return FALSE;
    }

    section.value = g_steal_pointer (&tmp);
    section.location = g_strdup (path);
    g_array_append_val (sections, section);
    memset (&section, 0, sizeof (section));
  }

  /* store the mapped file and the sections; note that the stored WpSpaJson
     still point to the data in the GMappedFile, so this is why we keep the
     GMappedFile alive */
  g_ptr_array_add (self->files, g_steal_pointer (&file));
  g_array_append_vals (self->conf_sections, sections->data, sections->len);
  g_array_set_clear_func (sections, NULL);

  return TRUE;
}

/*!
 * \brief Opens the configuration file and its fragments and keeps them
 *   mapped in memory for further access.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param error (out)(nullable): return location for a GError, or NULL
 * \returns TRUE on success, FALSE on error
 */
gboolean
wp_conf_open (WpConf * self, GError ** error)
{
  const gchar *no_frags = NULL;

  g_return_val_if_fail (WP_IS_CONF (self), FALSE);

  g_autofree gchar *path = NULL;
  g_autoptr (WpIterator) iterator = NULL;
  g_auto (GValue) value = G_VALUE_INIT;

  if (self->properties) {
    no_frags = wp_properties_get (self->properties, "no-fragments");
  }

  /*
   * open the config file - if the path supplied is absolute,
   * wp_base_dirs_find_file will ignore WP_BASE_DIRS_CONFIGURATION
   */
  path = wp_base_dirs_find_file (WP_BASE_DIRS_CONFIGURATION, NULL, self->name);
  if (path) {
    wp_info_object (self, "opening config file: %s", path);
    if (!open_and_load_sections (self, path, error))
      return FALSE;
  }
  g_clear_pointer (&path, g_free);

  /* open the .conf.d/ fragments */
  if (!no_frags) {
    path = g_strdup_printf ("%s.d", self->name);
    iterator = wp_base_dirs_new_files_iterator (WP_BASE_DIRS_CONFIGURATION, path,
        ".conf");

    for (; wp_iterator_next (iterator, &value); g_value_unset (&value)) {
      const gchar *filename = g_value_get_string (&value);

      wp_info_object (self, "opening fragment file: %s", filename);

      g_autoptr (GError) e = NULL;
      if (!open_and_load_sections (self, filename, &e)) {
        wp_warning_object (self, "failed to open '%s': %s", filename, e->message);
        continue;
      }
    }
  }

  if (self->files->len == 0) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
        "Could not locate configuration file '%s'", self->name);
    return FALSE;
  }

  return TRUE;
}

/*!
 * \brief Closes the configuration file and its fragments
 *
 * \ingroup wpconf
 * \param self the configuration
 */
void
wp_conf_close (WpConf * self)
{
  g_return_if_fail (WP_IS_CONF (self));

  g_array_set_size (self->conf_sections, 0);
  g_ptr_array_set_size (self->files, 0);
}

/*!
 * \brief Tests if the configuration files are open
 *
 * \ingroup wpconf
 * \param self the configuration
 * \returns TRUE if the configuration files are open, FALSE otherwise
 */
gboolean
wp_conf_is_open (WpConf * self)
{
  g_return_val_if_fail (WP_IS_CONF (self), FALSE);
  return self->files->len > 0;
}

/*!
 * \brief Gets the name of the configuration file
 *
 * \ingroup wpconf
 * \param self the configuration
 * \returns the name of the configuration file
 */
const gchar *
wp_conf_get_name (WpConf * self)
{
  g_return_val_if_fail (WP_IS_CONF (self), NULL);
  return self->name;
}

static WpSpaJson *
ensure_merged_section (WpConf * self, const gchar *section)
{
  g_autoptr (WpSpaJson) merged = NULL;
  WpConfSection *merged_section = NULL;

  /* check if the section is already merged */
  for (guint i = 0; i < self->conf_sections->len; i++) {
    WpConfSection *s = &g_array_index (self->conf_sections, WpConfSection, i);
    if (g_str_equal (s->name, section)) {
      if (!s->location) {
        wp_debug_object (self, "section %s is already merged", section);
        return wp_spa_json_ref (s->value);
      }
    }
  }

  /* Iterate over the sections and merge them */
  for (guint i = 0; i < self->conf_sections->len; i++) {
    WpConfSection *s = &g_array_index (self->conf_sections, WpConfSection, i);
    const gchar *s_name = s->name;

    /* skip the "override." prefix and take a note */
    gboolean override = g_str_has_prefix (s_name, OVERRIDE_SECTION_PREFIX);
    if (override)
      s_name += strlen (OVERRIDE_SECTION_PREFIX);

    if (g_str_equal (s_name, section)) {
      /* Merge sections if a previous value exists and
         the 'override.' prefix is not present */
      if (!override && merged) {
        g_autoptr (WpSpaJson) new_merged =
            wp_json_utils_merge_containers (merged, s->value);
        if (!merged) {
          wp_warning_object (self,
              "skipping merge of '%s' from '%s' as JSON containers are not compatible",
              section, s->location);
          continue;
        }

        g_clear_pointer (&merged, wp_spa_json_unref);
        merged = g_steal_pointer (&new_merged);
        merged_section = NULL;
      }
      /* Otherwise always replace */
      else {
        g_clear_pointer (&merged, wp_spa_json_unref);
        merged = wp_spa_json_ref (s->value);
        merged_section = s;
      }
    }
  }

  /* cache the result */
  if (merged_section) {
    /* if the merged json came from a single location, just clear
       the location from that WpConfSection to mark it as the result */
    wp_info_object (self, "section '%s' is used as-is from '%s'", section,
        merged_section->location);
    g_clear_pointer (&merged_section->location, g_free);
  } else if (merged) {
    /* if the merged json came from multiple locations, create a new
       WpConfSection to store it */
    WpConfSection s = { g_strdup (section), wp_spa_json_ref (merged), NULL };
    g_array_append_val (self->conf_sections, s);
    wp_info_object (self, "section '%s' is merged from multiple locations",
        section);
  } else {
    wp_info_object (self, "section '%s' is not defined", section);
  }

  return g_steal_pointer (&merged);
}

/*!
 * This method will get the JSON value of a specific section from the
 * configuration. If the same section is defined in multiple locations, the
 * sections with the same name will be either merged in case of arrays and
 * objects, or overridden in case of boolean, int, double and strings.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section the section name
 * \returns (transfer full) (nullable): the JSON value of the section or NULL
 *   if the section does not exist
 */
WpSpaJson *
wp_conf_get_section (WpConf *self, const gchar *section)
{
  g_return_val_if_fail (WP_IS_CONF (self), NULL);
  g_return_val_if_fail (section, NULL);

  return ensure_merged_section (self, section);
}

/*!
 * \brief Updates the given properties with the values of a specific section
 * from the configuration.
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param section the section name
 * \param props the properties to update
 * \returns the number of properties updated
 */
gint
wp_conf_section_update_props (WpConf *self, const gchar *section,
    WpProperties *props)
{
  g_autoptr (WpSpaJson) json = NULL;

  g_return_val_if_fail (WP_IS_CONF (self), -1);
  g_return_val_if_fail (section, -1);
  g_return_val_if_fail (props, -1);

  json = wp_conf_get_section (self, section);
  if (!json)
    return 0;
  return wp_properties_update_from_json (props, json);
}

#include "private/parse-conf-section.c"

/*!
 * \brief Parses standard pw_context sections from \a conf
 *
 * \ingroup wpconf
 * \param self the configuration
 * \param context the associated pw_context
 */
void
wp_conf_parse_pw_context_sections (WpConf * self, struct pw_context * context)
{
  gint res;
  WpProperties *conf_wp;
  struct pw_properties *conf_pw;

  g_return_if_fail (WP_IS_CONF (self));
  g_return_if_fail (context);

  /* convert needed sections into a pipewire-style conf dictionary */
  conf_wp = wp_properties_new ("config.path", "wpconf", NULL);
  {
    g_autoptr (WpSpaJson) j = wp_conf_get_section (self, "context.spa-libs");
    if (j) {
      g_autofree gchar *js = wp_spa_json_parse_string (j);
      wp_properties_set (conf_wp, "context.spa-libs", js);
    }
  }
  {
    g_autoptr (WpSpaJson) j = wp_conf_get_section (self, "context.modules");
    if (j) {
      g_autofree gchar *js = wp_spa_json_parse_string (j);
      wp_properties_set (conf_wp, "context.modules", js);
    }
  }
  conf_pw = wp_properties_unref_and_take_pw_properties (conf_wp);

  /* parse sections */
  if ((res = _pw_context_parse_conf_section (context, conf_pw, "context.spa-libs")) < 0)
    goto error;
  wp_info_object (self, "parsed %d context.spa-libs items", res);

  if ((res = _pw_context_parse_conf_section (context, conf_pw, "context.modules")) < 0)
    goto error;
  if (res > 0)
    wp_info_object (self, "parsed %d context.modules items", res);
  else
    wp_warning_object (self, "no modules loaded from context.modules");

out:
  pw_properties_free (conf_pw);
  return;

error:
  wp_critical_object (self, "failed to parse pw_context sections: %s",
      spa_strerror (res));
  goto out;
}
