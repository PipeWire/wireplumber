/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/* C++ STL */
#include <functional>

/* CPPTOML */
#include <include/cpptoml.h>

/* TOML */
#include "private.h"
#include "table.h"

namespace wp {
namespace toml {

/* The Table class */
class Table {
 public:
  /* The data of the array */
  using Data = std::shared_ptr<const cpptoml::table>;

  /* Constructor */
  Table(Data data) :
    data_(std::move(data)) {
  }

  /* Destructor */
  virtual ~Table() {
  }

  /* Determines if this table contains the given key */
  bool Contains(const std::string& key) const {
    return data_->contains(key);
  }

  /* Gets a value */
  template <typename T>
  bool GetValue(const std::string& key, T *val, bool qualified) const {
    g_return_val_if_fail (val, false);
    const cpptoml::option<T> opt =
        qualified ? data_->get_qualified_as<T>(key) : data_->get_as<T>(key);
    if (!opt)
      return false;
    *val = *opt;
    return true;
  }

  /* Gets an array of values */
  std::shared_ptr<const cpptoml::array> GetArray(const std::string& key,
      bool qualified) const {
    return qualified ? data_->get_array_qualified(key) : data_->get_array(key);
  }

  /* Gets an array of tables */
  std::shared_ptr<const cpptoml::table_array> GetTableArray(
      const std::string& key, bool qualified) const {
    return qualified ? data_->get_table_array_qualified(key) :
        data_->get_table_array(key);
  }

  /* Gets a nested table */
  Data GetTable(const std::string& key, bool qualified) const {
    return qualified ? data_->get_table_qualified(key) : data_->get_table(key);
  }

 private:
  /* Copy Constructor */
  Table(const Table&) = delete;

  /* Move Constructor */
  Table(Table &&) = delete;

  /* Copy-Assign Constructor */
  Table& operator=(const Table&) = delete;

  /* Move-Assign Constructr */
  Table& operator=(Table &&) = delete;

 private:
  /* The data table */
  const Data data_;
};

/* The Array Table class */
class TableArray {
 public:
  /* The data of the array */
  using Data = std::shared_ptr<const cpptoml::table_array>;

  /* The for each function for arrays of tables */
  using ForEachFunction = std::function<void(WpTomlTable *, gpointer)>;

  /* Constructor */
  TableArray(Data data) :
      data_(std::move(data)) {
  }

  /* Destructor */
  virtual ~TableArray() {
  }

  /* Calls the given callback for arrays of values */
  void ForEach(ForEachFunction func, gpointer user_data) const {
    for (const auto& table : *data_) {
      gconstpointer p = static_cast<gconstpointer>(&table);
      g_autoptr (WpTomlTable) t = wp_toml_table_new(p);
      func(t, user_data);
    }
  }

 private:
  /* Copy Constructor */
  TableArray(const TableArray&) = delete;

  /* Move Constructor */
  TableArray(TableArray &&) = delete;

  /* Copy-Assign Constructor */
  TableArray& operator=(const TableArray&) = delete;

  /* Move-Assign Constructr */
  TableArray& operator=(TableArray &&) = delete;

 private:
  /* The data array */
  const Data data_;
};

}  /* namespace toml */
}  /* namespace wp */

struct _WpTomlTable
{
  wp::toml::Table *data;
};

G_DEFINE_BOXED_TYPE(WpTomlTable, wp_toml_table, wp_toml_table_ref,
  wp_toml_table_unref)

struct _WpTomlTableArray
{
  const wp::toml::TableArray *data;
};

G_DEFINE_BOXED_TYPE(WpTomlTableArray, wp_toml_table_array,
    wp_toml_table_array_ref, wp_toml_table_array_unref)

WpTomlTable *
wp_toml_table_new (gconstpointer data)
{
  g_return_val_if_fail (data, nullptr);

  try {
    g_autoptr (WpTomlTable) self = g_rc_box_new (WpTomlTable);

    /* Set the data */
    const wp::toml::Table::Data *d =
        static_cast<const wp::toml::Table::Data *>(data);
    self->data = new wp::toml::Table {*d};

    return static_cast<WpTomlTable *>(g_steal_pointer (&self));
  } catch (std::bad_alloc& ba) {
    g_critical ("Could not create WpTomlTable: %s", ba.what());
    return nullptr;
  } catch (...) {
    g_critical ("Could not create WpTomlTable");
    return nullptr;
  }
}

WpTomlTable *
wp_toml_table_ref (WpTomlTable * self)
{
  return static_cast<WpTomlTable *>(
    g_rc_box_acquire (static_cast<gpointer>(self)));
}

void
wp_toml_table_unref (WpTomlTable * self)
{
  static void (*free_func)(gpointer) = [](gpointer p){
    WpTomlTable *t = static_cast<WpTomlTable *>(p);
    delete t->data;
  };
  g_rc_box_release_full (self, free_func);
}

static WpTomlTableArray *
wp_toml_table_array_new (gconstpointer data)
{
  g_return_val_if_fail (data, nullptr);

  try {
    g_autoptr (WpTomlTableArray) self = g_rc_box_new (WpTomlTableArray);

    /* Set the data */
    const wp::toml::TableArray::Data *d =
        static_cast<const wp::toml::TableArray::Data *>(data);
    self->data = new wp::toml::TableArray {*d};

    return static_cast<WpTomlTableArray *>(g_steal_pointer (&self));
  } catch (std::bad_alloc& ba) {
    g_critical ("Could not create WpTomlTableArray: %s", ba.what());
    return nullptr;
  } catch (...) {
    g_critical ("Could not create WpTomlTableArray");
    return nullptr;
  }
}

WpTomlTableArray *
wp_toml_table_array_ref (WpTomlTableArray * self)
{
  return static_cast<WpTomlTableArray *>(
    g_rc_box_acquire (static_cast<gpointer>(self)));
}

void
wp_toml_table_array_unref (WpTomlTableArray * self)
{
  static void (*free_func)(gpointer) = [](gpointer p){
    WpTomlTableArray *at = static_cast<WpTomlTableArray *>(p);
    delete at->data;
  };
  g_rc_box_release_full (self, free_func);
}

gboolean
wp_toml_table_contains (const WpTomlTable *self, const char *key) {
  return self->data->Contains(key);
}

gboolean
wp_toml_table_get_boolean (const WpTomlTable *self, const char *key,
  gboolean *val)
{
  bool v;
  if (!self->data->GetValue<bool>(key, &v, false))
    return false;
  *val = v ? TRUE : FALSE;
  return true;
}

gboolean
wp_toml_table_get_qualified_boolean (const WpTomlTable *self, const char *key,
    gboolean *val)
{
  bool v;
  if (!self->data->GetValue<bool>(key, &v, true))
    return false;
  *val = v ? TRUE : FALSE;
  return true;
}

gboolean
wp_toml_table_get_int8 (const WpTomlTable *self, const char *key, int8_t *val)
{
  return self->data->GetValue<int8_t>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_int8 (const WpTomlTable *self, const char *key,
    int8_t *val)
{
  return self->data->GetValue<int8_t>(key, val, true);
}

gboolean
wp_toml_table_get_uint8 (const WpTomlTable *self, const char *key, uint8_t *val)
{
  return self->data->GetValue<uint8_t>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_uint8 (const WpTomlTable *self, const char *key,
    uint8_t *val)
{
  return self->data->GetValue<uint8_t>(key, val, true);
}

gboolean
wp_toml_table_get_int16 (const WpTomlTable *self, const char *key, int16_t *val)
{
  return self->data->GetValue<int16_t>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_int16 (const WpTomlTable *self, const char *key,
    int16_t *val)
{
  return self->data->GetValue<int16_t>(key, val, true);
}

gboolean
wp_toml_table_get_uint16 (const WpTomlTable *self, const char *key,
    uint16_t *val)
{
  return self->data->GetValue<uint16_t>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_uint16 (const WpTomlTable *self, const char *key,
    uint16_t *val)
{
  return self->data->GetValue<uint16_t>(key, val, true);
}

gboolean
wp_toml_table_get_int32 (const WpTomlTable *self, const char *key, int32_t *val)
{
  return self->data->GetValue<int32_t>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_int32 (const WpTomlTable *self, const char *key,
    int32_t *val)
{
  return self->data->GetValue<int32_t>(key, val, true);
}

gboolean
wp_toml_table_get_uint32 (const WpTomlTable *self, const char *key,
    uint32_t *val)
{
  return self->data->GetValue<uint32_t>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_uint32 (const WpTomlTable *self, const char *key,
    uint32_t *val)
{
  return self->data->GetValue<uint32_t>(key, val, true);
}

gboolean
wp_toml_table_get_int64 (const WpTomlTable *self, const char *key, int64_t *val)
{
  return self->data->GetValue<int64_t>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_int64 (const WpTomlTable *self, const char *key,
    int64_t *val)
{
  return self->data->GetValue<int64_t>(key, val, true);
}

gboolean
wp_toml_table_get_uint64 (const WpTomlTable *self, const char *key,
    uint64_t *val)
{
  return self->data->GetValue<uint64_t>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_uint64 (const WpTomlTable *self, const char *key,
    uint64_t *val)
{
  return self->data->GetValue<uint64_t>(key, val, true);
}

gboolean
wp_toml_table_get_double (const WpTomlTable *self, const char *key, double *val)
{
  return self->data->GetValue<double>(key, val, false);
}

gboolean
wp_toml_table_get_qualified_double (const WpTomlTable *self, const char *key,
    double *val)
{
  return self->data->GetValue<double>(key, val, true);
}

char *
wp_toml_table_get_string (const WpTomlTable *self, const char *key)
{
  std::string str;
  return self->data->GetValue<std::string>(key, &str, false) ?
      g_strdup (str.c_str()) : nullptr;
}

char *
wp_toml_table_get_qualified_string (const WpTomlTable *self, const char *key)
{
  std::string str;
  return self->data->GetValue<std::string>(key, &str, true) ?
      g_strdup (str.c_str()) : nullptr;
}

WpTomlArray *
wp_toml_table_get_array (const WpTomlTable *self, const char *key)
{
  std::shared_ptr<const cpptoml::array> array =
      self->data->GetArray(key, false);
  return array ?
      wp_toml_array_new (static_cast<gconstpointer>(&array)) :
      nullptr;
}

WpTomlArray *
wp_toml_table_get_qualified_array (const WpTomlTable *self, const char *key)
{
  std::shared_ptr<const cpptoml::array> array =
      self->data->GetArray(key, true);
  return array ?
      wp_toml_array_new (static_cast<gconstpointer>(&array)) :
      nullptr;
}

WpTomlTable *
wp_toml_table_get_table (const WpTomlTable *self, const char *key)
{
  wp::toml::Table::Data table = self->data->GetTable(key, false);
  return table ?
      wp_toml_table_new (static_cast<gconstpointer>(&table)) :
      nullptr;
}

WpTomlTable *
wp_toml_table_get_qualified_table (const WpTomlTable *self, const char *key)
{
  wp::toml::Table::Data table = self->data->GetTable(key, true);
  return table ?
      wp_toml_table_new (static_cast<gconstpointer>(&table)) :
      nullptr;
}

WpTomlTableArray *
wp_toml_table_get_array_table (const WpTomlTable *self, const char *key)
{
  std::shared_ptr<const cpptoml::table_array> array_table =
      self->data->GetTableArray(key, false);
  return array_table ?
      wp_toml_table_array_new (static_cast<gconstpointer>(&array_table)) :
      nullptr;
}

WpTomlTableArray *
wp_toml_table_get_qualified_array_table (const WpTomlTable *self,
    const char *key)
{
  std::shared_ptr<const cpptoml::table_array> array_table =
      self->data->GetTableArray(key, true);
  return array_table ?
      wp_toml_table_array_new (static_cast<gconstpointer>(&array_table)) :
      nullptr;
}

void
wp_toml_table_array_for_each (const WpTomlTableArray *self,
    WpTomlTableArrayForEachFunc func, gpointer user_data)
{
  self->data->ForEach(func, user_data);
}
