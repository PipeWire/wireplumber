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
#include "array.h"

namespace wp {
namespace toml {

/* The Array class */
class Array {
 public:
  /* The data of the array */
  using Data = std::shared_ptr<const cpptoml::array>;

  /* The for each function for values */
  template <typename T>
  using ForEachValueFunction = std::function<void(const T*, gpointer)>;

  /* The for each function for arrays of values */
  using ForEachArrayFunction = std::function<void(WpTomlArray *, gpointer )>;

  /* Constructor */
  Array(Data data) :
      data_(std::move(data)) {
  }

  /* Destructor */
  virtual ~Array() {
  }

  /* Calls the given callback for values */
  template <typename T>
  void ForEachValue(ForEachValueFunction<T> func, gpointer user_data) const {
    for (const std::shared_ptr<cpptoml::value<T>>& v : data_->array_of<T>()) {
      if (v) {
        const T val = v->get();
        func(&val, user_data);
      } else {
        func(nullptr, user_data);
      }
    }
  }

  /* Calls the given callback for arrays of values */
  void ForEachArray(ForEachArrayFunction func, gpointer user_data) const {
    for (const Data& val : data_->nested_array()) {
      gconstpointer d = static_cast<gconstpointer>(&val);
      g_autoptr (WpTomlArray) a = wp_toml_array_new(d);
      func(a, user_data);
    }
  }

 private:
  /* Copy Constructor */
  Array(const Array&) = delete;

  /* Move Constructor */
  Array(Array &&) = delete;

  /* Copy-Assign Constructor */
  Array& operator=(const Array&) = delete;

  /* Move-Assign Constructr */
  Array& operator=(Array &&) = delete;

 private:
  /* The data array */
  const Data data_;
};

}  /* namespace toml */
}  /* namespace wp */

struct _WpTomlArray
{
  const wp::toml::Array *data;
};

G_DEFINE_BOXED_TYPE(WpTomlArray, wp_toml_array, wp_toml_array_ref,
    wp_toml_array_unref)

WpTomlArray *
wp_toml_array_new (gconstpointer data)
{
  g_return_val_if_fail (data, nullptr);

  try {
    g_autoptr(WpTomlArray) self = g_rc_box_new (WpTomlArray);

    /* Set the data */
    const wp::toml::Array::Data *d =
        static_cast<const wp::toml::Array::Data *>(data);
    self->data = new wp::toml::Array {*d};

    return static_cast<WpTomlArray *>(g_steal_pointer (&self));
  } catch (std::bad_alloc& ba) {
    g_critical ("Could not create WpTomlArray: %s", ba.what());
    return nullptr;
  } catch (...) {
    g_critical ("Could not create WpTomlArray");
    return nullptr;
  }
}

WpTomlArray *
wp_toml_array_ref (WpTomlArray * self)
{
  return static_cast<WpTomlArray *>(
    g_rc_box_acquire (static_cast<gpointer>(self)));
}

void
wp_toml_array_unref (WpTomlArray * self)
{
  static void (*free_func)(gpointer) = [](gpointer p){
    WpTomlArray *a = static_cast<WpTomlArray *>(p);
    delete a->data;
  };
  g_rc_box_release_full (self, free_func);
}

void
wp_toml_array_for_each_boolean (const WpTomlArray *self,
    WpTomlArrayForEachBoolFunc func, gpointer user_data)
{
  self->data->ForEachValue<bool>([&](const bool *v, gpointer d){
      if (v) {
        const gboolean b = *v ? TRUE : FALSE;
        func(&b, d);
      } else {
        func(nullptr, d);
      }
    }, user_data);
}

void
wp_toml_array_for_each_int64 (const WpTomlArray *self,
    WpTomlArrayForEachInt64Func func, gpointer user_data)
{
  self->data->ForEachValue<int64_t>(func, user_data);
}

void
wp_toml_array_for_each_double (const WpTomlArray *self,
    WpTomlArrayForEachDoubleFunc func, gpointer user_data)
{
  self->data->ForEachValue<double>(func, user_data);
}

void
wp_toml_array_for_each_string (const WpTomlArray *self,
    WpTomlArrayForEachStringFunc func, gpointer user_data)
{
  self->data->ForEachValue<std::string>([&](const std::string *v, gpointer d){
      func(v ? v->c_str() : nullptr, d);
    }, user_data);
}

void
wp_toml_array_for_each_array (const WpTomlArray *self,
    WpTomlArrayForEachArrayFunc func, gpointer user_data)
{
  self->data->ForEachArray(func, user_data);
}

