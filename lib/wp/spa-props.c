/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define G_LOG_DOMAIN "wp-spa-props"

#include "private.h"
#include <spa/pod/pod.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/param/props.h>
#include <spa/utils/defs.h>
#include <spa/utils/result.h>

#include "spa-pod.h"
#include "spa-type.h"

struct entry
{
  gchar *id_name;
  gchar *description;
  WpSpaPod *value;
};

struct entry *
entry_new (void)
{
  struct entry *e = g_slice_new0 (struct entry);
  return e;
}

static void
entry_free (struct entry *e)
{
  g_free (e->id_name);
  g_free (e->description);
  g_clear_pointer (&e->value, wp_spa_pod_unref);
  g_slice_free (struct entry, e);
}

void
wp_spa_props_clear (WpSpaProps * self)
{
  g_list_free_full (self->entries, (GDestroyNotify) entry_free);
  self->entries = NULL;
}

// Takes ownership of pod
void
wp_spa_props_register (WpSpaProps * self, const char *id_name,
    const gchar *description, WpSpaPod *pod)
{
  struct entry *e = entry_new ();
  e->id_name = g_strdup (id_name);
  e->description = g_strdup (description);
  e->value = pod;
  self->entries = g_list_append (self->entries, e);
}

gboolean
wp_spa_props_register_from_prop_info (WpSpaProps * self,
    const WpSpaPod * prop_info)
{
  guint32 id;
  const gchar *id_name, *description;
  g_autoptr (WpSpaPod) type = NULL;


  if (!wp_spa_pod_get_object (prop_info,
      "PropInfo", NULL,
      "id", "I", &id,
      "name", "s", &description,
      "type", "P", &type,
      NULL)) {
    g_assert_true (FALSE);
    g_warning ("Bad prop info object");
    return FALSE;
  }

  if (!wp_spa_type_get_by_id (WP_SPA_TYPE_TABLE_PROPS, id, NULL, &id_name,
      NULL)) {
    g_warning ("Id '%d' is not registered", id);
    return FALSE;
  }

  wp_spa_props_register (self, id_name, description, g_steal_pointer (&type));
  return TRUE;
}

// get <-- cached
WpSpaPod *
wp_spa_props_get_stored (WpSpaProps * self, const char * id_name)
{
  GList *l = self->entries;
  struct entry * e;
  while (l && g_strcmp0 (((struct entry *) l->data)->id_name, id_name) != 0)
    l = g_list_next (l);
  if (!l)
    return NULL;

  e = (struct entry *) l->data;
  return wp_spa_pod_is_choice (e->value) ?
      wp_spa_pod_get_choice_child (e->value) : wp_spa_pod_ref (e->value);
}

// exported set --> cache + update(variant to pod -> push)
gboolean
wp_spa_props_store (WpSpaProps * self, const char * id_name,
    const WpSpaPod *value)
{
  GList *l = self->entries;
  struct entry * e;
  g_autoptr (WpSpaPod) pod = NULL;

  while (l && g_strcmp0 (((struct entry *) l->data)->id_name, id_name) != 0)
    l = g_list_next (l);
  if (!l)
    return FALSE;

  e = (struct entry *) l->data;

  pod = wp_spa_pod_is_choice (e->value) ?
      wp_spa_pod_get_choice_child (e->value) : wp_spa_pod_ref (e->value);

  return !wp_spa_pod_equal (pod, value) && wp_spa_pod_set_pod (pod, value);
}

// exported event set --> pod to variant -> cache
// proxy event param --> pod to variant -> cache
gboolean
wp_spa_props_store_from_props (WpSpaProps * self, const WpSpaPod * props,
    GPtrArray * changed_ids)
{
  g_autoptr (WpSpaPod) pod = NULL;
  g_autoptr (WpIterator) it = NULL;
  GValue next = G_VALUE_INIT;

  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (props, FALSE);
  g_return_val_if_fail (changed_ids, FALSE);

  if (g_strcmp0 (wp_spa_pod_get_object_type_name (props), "Props") != 0)
    return FALSE;

  it = wp_spa_pod_iterator_new (props);
  while (wp_iterator_next (it, &next)) {
    WpSpaPod *p = g_value_get_boxed (&next);
    const char *key_name = NULL;
    g_autoptr (WpSpaPod) v = NULL;
    wp_spa_pod_get_property (p, &key_name, &v);

    if (wp_spa_props_store (self, key_name, v) && changed_ids)
      g_ptr_array_add (changed_ids, g_strdup (key_name));

    g_value_unset (&next);
  }

  return TRUE;
}

WpSpaPod *
wp_spa_props_build_props (WpSpaProps * self)
{
  g_autoptr (WpSpaPodBuilder) b = NULL;
  GList *l;

  g_return_val_if_fail (self, NULL);

  b = wp_spa_pod_builder_new_object ("Props", "Props");
  for (l = self->entries; l != NULL; l = g_list_next (l)) {
    struct entry * e = (struct entry *) l->data;
    if (e->id_name && e->value) {
      g_autoptr (WpSpaPod) pod = wp_spa_pod_is_choice (e->value) ?
          wp_spa_pod_get_choice_child (e->value) : wp_spa_pod_ref (e->value);
      wp_spa_pod_builder_add_property (b, e->id_name);
      wp_spa_pod_builder_add_pod (b, pod);
    }
  }

  return wp_spa_pod_builder_end (b);
}

GPtrArray *
wp_spa_props_build_propinfo (WpSpaProps * self)
{
  GPtrArray *res = g_ptr_array_new_with_free_func (
      (GDestroyNotify) wp_spa_pod_unref);
  GList *l;

  for (l = self->entries; l != NULL; l = g_list_next (l)) {
    struct entry * e = (struct entry *) l->data;
    guint32 id;
    if (!wp_spa_type_get_by_nick (WP_SPA_TYPE_TABLE_PROPS, e->id_name, &id,
        NULL, NULL)) {
      g_warning ("Id name '%s' is not registered", e->id_name);
      continue;
    }

    g_ptr_array_add (res, wp_spa_pod_new_object (
        "PropInfo", "PropInfo",
        "id", "I", id,
        "name", "s", e->description,
        "type", "P", e->value,
        NULL));
  }

  return res;
}

// for exported update / prop_info + props
GPtrArray *
wp_spa_props_build_all_pods (WpSpaProps * self)
{
  GPtrArray *res = wp_spa_props_build_propinfo (self);
  g_ptr_array_insert (res, 0, wp_spa_props_build_props (self));
  return res;
}
