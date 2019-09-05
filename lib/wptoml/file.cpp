/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/* CPPTOML */
#include <include/cpptoml.h>

/* TOML */
#include "private.h"
#include "file.h"

struct _WpTomlFile
{
  char *name;
  WpTomlTable *table;
};

G_DEFINE_BOXED_TYPE(WpTomlFile, wp_toml_file, wp_toml_file_ref,
    wp_toml_file_unref)

WpTomlFile *
wp_toml_file_new (const char *name)
{
  g_return_val_if_fail (name, nullptr);

  try {
    g_autoptr (WpTomlFile) self = g_rc_box_new (WpTomlFile);

    /* Set the name */
    self->name = g_strdup (name);

    /* Set the table by parsing the file */
    std::shared_ptr<cpptoml::table> data = cpptoml::parse_file(name);
    self->table = wp_toml_table_new (static_cast<gconstpointer>(&data));

    return static_cast<WpTomlFile *>(g_steal_pointer (&self));
  } catch (std::bad_alloc& ba) {
    g_critical ("Could not create WpTomlFile from '%s': %s", name, ba.what());
    return nullptr;
  } catch (...) {
    g_critical ("Could not create WpTomlFile from '%s'", name);
    return nullptr;
  }
}

WpTomlFile *
wp_toml_file_ref (WpTomlFile * self)
{
  return static_cast<WpTomlFile *>(
    g_rc_box_acquire (static_cast<gpointer>(self)));
}

void
wp_toml_file_unref (WpTomlFile * self)
{
  static void (*free_func)(gpointer) = [](gpointer p){
    WpTomlFile *f = static_cast<WpTomlFile *>(p);
    g_free (f->name);
    f->name = nullptr;
    wp_toml_table_unref (f->table);
    f->table = nullptr;
  };
  g_rc_box_release_full (self, free_func);
}

const char *
wp_toml_file_get_name (const WpTomlFile *self)
{
  return self->name;
}

WpTomlTable *
wp_toml_file_get_table (const WpTomlFile *self)
{
  return wp_toml_table_ref (self->table);
}
