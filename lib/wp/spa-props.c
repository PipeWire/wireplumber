/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "private.h"
#include <spa/pod/pod.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/param/props.h>
#include <spa/utils/defs.h>
#include <spa/utils/result.h>

struct entry
{
  guint32 id;
  gchar *name;
  struct spa_pod *type;
  struct spa_pod *value;
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
  g_free (e->name);
  free (e->type);
  free (e->value);
  g_slice_free (struct entry, e);
}

void
wp_spa_props_clear (WpSpaProps * self)
{
  g_list_free_full (self->entries, (GDestroyNotify) entry_free);
  self->entries = NULL;
}

void
wp_spa_props_register_pod (WpSpaProps * self,
    guint32 id, const gchar *name, const struct spa_pod *type)
{
  struct entry *e = entry_new ();
  e->id = id;
  e->name = g_strdup (name);
  e->type = spa_pod_copy (type);

  if (!spa_pod_is_choice (type))
    e->value = spa_pod_copy (type);
  else
    e->value = spa_pod_copy (SPA_POD_CHOICE_CHILD (type));

  self->entries = g_list_append (self->entries, e);
}

gint
wp_spa_props_register_from_prop_info (WpSpaProps * self,
    const struct spa_pod * prop_info)
{
  guint32 id;
  gchar *name;
  const struct spa_pod *type;
  int res;

  res = spa_pod_parse_object (prop_info,
      SPA_TYPE_OBJECT_PropInfo, NULL,
      SPA_PROP_INFO_id,   SPA_POD_Id (&id),
      SPA_PROP_INFO_name, SPA_POD_String (&name),
      SPA_PROP_INFO_type, SPA_POD_Pod (&type));

  if (res < 0) {
    g_debug ("Bad prop info object");
    return res;
  }

  wp_spa_props_register_pod (self, id, name, type);
  return 0;
}

// get <-- cached
const struct spa_pod *
wp_spa_props_get_stored (WpSpaProps * self, guint32 id)
{
  GList *l = self->entries;
  while (l && ((struct entry *) l->data)->id != id)
    l = g_list_next (l);

  return l ? ((struct entry *) l->data)->value : NULL;
}

// exported set --> cache + update(variant to pod -> push)
gint
wp_spa_props_store_pod (WpSpaProps * self, guint32 id,
    const struct spa_pod * value)
{
  GList *l = self->entries;
  struct entry * e;
  uint32_t expected_type;

  while (l && ((struct entry *) l->data)->id != id)
    l = g_list_next (l);

  if (!l)
    return -ESRCH;

  e = (struct entry *) l->data;

  expected_type = spa_pod_is_choice (e->type) ?
      SPA_POD_CHOICE_VALUE_TYPE (e->type) : SPA_POD_TYPE (e->type);
  if (SPA_POD_TYPE (value) != expected_type)
    return -EINVAL;

#define GET_VAL(pod, type) ((struct spa_pod_##type *) pod)->value

  switch (SPA_POD_TYPE (value)) {
    //TODO bounds checking on integer types
  case SPA_TYPE_Id:
    if (GET_VAL (e->value, id) != GET_VAL (value, id)) {
      GET_VAL (e->value, id) = GET_VAL (value, id);
      return 1;
    }
    break;
  case SPA_TYPE_Bool:
    if (GET_VAL (e->value, bool) != GET_VAL (value, bool)) {
      GET_VAL (e->value, bool) = GET_VAL (value, bool);
      return 1;
    }
    break;
  case SPA_TYPE_Int:
    if (GET_VAL (e->value, int) != GET_VAL (value, int)) {
      GET_VAL (e->value, int) = GET_VAL (value, int);
      return 1;
    }
    break;
  case SPA_TYPE_Long:
    if (GET_VAL (e->value, long) != GET_VAL (value, long)) {
      GET_VAL (e->value, long) = GET_VAL (value, long);
      return 1;
    }
    break;
  case SPA_TYPE_Fd:
    if (GET_VAL (e->value, fd) != GET_VAL (value, fd)) {
      GET_VAL (e->value, fd) = GET_VAL (value, fd);
      return 1;
    }
    break;
  case SPA_TYPE_Float:
    if (GET_VAL (e->value, float) != GET_VAL (value, float)) {
      GET_VAL (e->value, float) = GET_VAL (value, float);
      return 1;
    }
    break;
  case SPA_TYPE_Double:
    if (GET_VAL (e->value, double) != GET_VAL (value, double)) {
      GET_VAL (e->value, double) = GET_VAL (value, double);
      return 1;
    }
    break;
  case SPA_TYPE_Rectangle:
    if (GET_VAL (e->value, rectangle).width != GET_VAL (value, rectangle).width ||
        GET_VAL (e->value, rectangle).height != GET_VAL (value, rectangle).height) {
      GET_VAL (e->value, rectangle) = GET_VAL (value, rectangle);
      return 1;
    }
    break;
  case SPA_TYPE_Fraction:
    if (GET_VAL (e->value, fraction).num != GET_VAL (value, fraction).num ||
        GET_VAL (e->value, fraction).denom != GET_VAL (value, fraction).denom) {
      GET_VAL (e->value, fraction) = GET_VAL (value, fraction);
      return 1;
    }
    break;
  default:
    g_clear_pointer (&e->value, free);
    e->value = spa_pod_copy (value);
    return 1;
  }

#undef GET_VAL

  return 0;
}

// exported event set --> pod to variant -> cache
// proxy event param --> pod to variant -> cache
gint
wp_spa_props_store_from_props (WpSpaProps * self, const struct spa_pod * props,
    GArray * changed_ids)
{
  const struct spa_pod_object *obj;
  const struct spa_pod_prop *iter;
  gint ret, count = 0;

  g_return_val_if_fail (!changed_ids ||
      g_array_get_element_size (changed_ids) == sizeof (uint32_t), -EINVAL);

  if (!spa_pod_is_object_type (props, SPA_TYPE_OBJECT_Props))
    return -EINVAL;

  obj = (const struct spa_pod_object *) props;
  iter = spa_pod_prop_first (&obj->body);

  for (iter = spa_pod_prop_first (&obj->body);
       spa_pod_prop_is_inside (&obj->body, obj->pod.size, iter);
       iter = spa_pod_prop_next (iter)) {
    if ((ret = wp_spa_props_store_pod (self, iter->key, &iter->value)) < 0) {
      g_debug ("error storing property 0x%x: %s", iter->key,
          spa_strerror (ret));
    } else if (ret == 1 && changed_ids) {
      g_array_append_val (changed_ids, iter->key);
      count++;
    }
  }

  return count;
}

// for exported update / prop_info + props
GPtrArray *
wp_spa_props_build_all_pods (WpSpaProps * self, struct spa_pod_builder * b)
{
  GPtrArray *res = g_ptr_array_new ();
  GList *l;
  struct spa_pod_frame f;
  struct spa_pod *pod;

  /* Props */
  spa_pod_builder_push_object (b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
  for (l = self->entries; l != NULL; l = g_list_next (l)) {
    struct entry * e = (struct entry *) l->data;
    if (e->value) {
      spa_pod_builder_prop (b, e->id, 0);
      spa_pod_builder_primitive (b, e->value);
    }
  }
  pod = spa_pod_builder_pop (b, &f);
  g_ptr_array_add (res, pod);

  /* PropInfo */
  for (l = self->entries; l != NULL; l = g_list_next (l)) {
    struct entry * e = (struct entry *) l->data;
    pod = spa_pod_builder_add_object (b,
        SPA_TYPE_OBJECT_PropInfo, SPA_PARAM_PropInfo,
        SPA_PROP_INFO_id,   SPA_POD_Id (e->id),
        SPA_PROP_INFO_name, SPA_POD_String (e->name),
        SPA_PROP_INFO_type, SPA_POD_Pod (e->type));
    g_ptr_array_add (res, pod);
  }

  return res;
}

// proxy set --> value to props object -> push
struct spa_pod *
wp_spa_props_build_update (WpSpaProps * self, guint32 id,
    const struct spa_pod * value, struct spa_pod_builder * b)
{
  struct spa_pod_frame f;

  spa_pod_builder_push_object (b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
  spa_pod_builder_prop (b, id, 0);
  spa_pod_builder_primitive (b, value);
  return spa_pod_builder_pop (b, &f);
}

const struct spa_pod *
wp_spa_props_build_pod_valist (gchar * buffer, gsize size, va_list args)
{
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, size);
  struct spa_pod_frame f;
  void *pod;

  spa_pod_builder_push_struct (&b, &f);
  spa_pod_builder_addv (&b, args);
  pod = spa_pod_builder_pop (&b, &f);
  return SPA_POD_CONTENTS (struct spa_pod_struct, pod);
}
