/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Arnaud Ferraris <arnaud.ferraris@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

#include <spa/param/audio/raw.h>
#include <spa/param/audio/type-info.h>
#include <spa/debug/types.h>
#include <spa/utils/json.h>

#define STATE_NAME "default-routes"
#define SAVE_INTERVAL_MS 1000

G_DEFINE_QUARK (wp-module-default-routes-routes, routes);

/* Signals */
enum
{
  SIGNAL_GET_ROUTES,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DECLARE_DERIVABLE_TYPE (WpDefaultRoutes, wp_default_routes, WP,
    DEFAULT_ROUTES, WpPlugin)

struct _WpDefaultRoutesClass
{
  WpPluginClass parent_class;

  void (*get_routes) (WpDefaultRoutes *self, WpPipewireObject *device,
      GHashTable **routes);
};

typedef struct _WpDefaultRoutesPrivate WpDefaultRoutesPrivate;
struct _WpDefaultRoutesPrivate
{
  WpState *state;
  WpProperties *routes;
  WpProperties *props;
  GSource *routes_timeout;
  GSource *props_timeout;

  GHashTable *current_routes;
  GHashTable *default_routes;

  WpObjectManager *devices_om;
};

G_DEFINE_TYPE_WITH_PRIVATE (WpDefaultRoutes, wp_default_routes,
    WP_TYPE_PLUGIN)

#define ALLOW_CHARS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_."
#define MAX_JSON_STRING_LEN 256

static gint
find_device_route (WpPipewireObject *device, const gchar *lookup_name,
    gint lookup_device)
{
  WpIterator *routes = NULL;
  g_auto (GValue) item = G_VALUE_INIT;

  routes = g_object_get_qdata (G_OBJECT (device), routes_quark ());
  g_return_val_if_fail (routes, -1);

  wp_iterator_reset (routes);
  for (; wp_iterator_next (routes, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    gint index = 0;
    const gchar *name = NULL;
    guint size, type, num, i;
    gint *devlist = NULL;

    /* Parse */
    if (!wp_spa_pod_get_object (pod, NULL,
        "index", "i", &index,
        "name", "s", &name,
        "devices", "a", &size, &type, &num, &devlist,
        NULL)) {
      continue;
    }

    for (i = 0; i < num; i++) {
      if (devlist[i] == lookup_device && g_strcmp0 (name, lookup_name) == 0)
        return index;
    }
  }

  return -1;
}

static gboolean
timeout_save_properties_cb (WpDefaultRoutes *self)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);

  if (!wp_state_save (priv->state, "properties", priv->props))
    wp_warning_object (self, "could not save properties");

  return G_SOURCE_REMOVE;
}

static void
timeout_save_properties (WpDefaultRoutes *self, guint ms)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  g_return_if_fail (core);
  g_return_if_fail (priv->props);

  /* Clear the current timeout callback */
  if (priv->props_timeout)
      g_source_destroy (priv->props_timeout);
  g_clear_pointer (&priv->props_timeout, g_source_unref);

  /* Add the timeout callback */
  wp_core_timeout_add_closure (core, &priv->props_timeout, ms,
      g_cclosure_new_object (G_CALLBACK (timeout_save_properties_cb),
      G_OBJECT (self)));
}

static uint32_t
channel_from_name (const char *name)
{
  for (gint i = 0; spa_type_audio_channel[i].name; i++) {
    if (g_strcmp0 (name, spa_debug_type_short_name (spa_type_audio_channel[i].name)) == 0)
      return spa_type_audio_channel[i].type;
	}
	return SPA_AUDIO_CHANNEL_UNKNOWN;
}

static const char *
channel_to_name(guint channel)
{
  for (gint i = 0; spa_type_audio_channel[i].name; i++) {
    if (spa_type_audio_channel[i].type == channel)
      return spa_debug_type_short_name (spa_type_audio_channel[i].name);
  }
  return "UNK";
}

static GArray *
parse_channel_volumes(const gchar *chvol_str)
{
  struct spa_json array;
  struct spa_json item;
  GArray *values = NULL;
  float val;

  spa_json_init (&array, chvol_str, strlen (chvol_str));
  if (spa_json_enter_array (&array, &item) <= 0)
    return NULL;

  values = g_array_new (FALSE, FALSE, sizeof (float));
  while (spa_json_get_float (&item, &val) > 0)
    g_array_append_val (values, val);

  return values;
}

static GPtrArray *
parse_channel_map (const gchar *chmap_str)
{
  struct spa_json array;
  struct spa_json item;
  GPtrArray *values;
  char val[MAX_JSON_STRING_LEN];

  spa_json_init (&array, chmap_str, strlen (chmap_str));
  if (spa_json_enter_array (&array, &item) <= 0)
    return NULL;

  values = g_ptr_array_new ();
  g_ptr_array_set_free_func (values, g_free);
  while (spa_json_get_string (&item, val, MAX_JSON_STRING_LEN) > 0)
    g_ptr_array_add (values, g_strdup (val));

  return values;
}

static void
apply_routes_properties (WpDefaultRoutes *self, WpPipewireObject *device,
    const gchar *name, WpDirection direction, gint device_id)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);
  g_autoptr (WpSpaPodBuilder) builder =
      wp_spa_pod_builder_new_object ("Spa:Pod:Object:Param:Props", "Route");
  g_autoptr (WpSpaPod) props = NULL;
  const gchar *dev_name = NULL;
  const gchar *prop_value = NULL;
  gchar *prop_name = NULL;
  gchar *routes_name = NULL;
  gchar *dir_str = direction == WP_DIRECTION_INPUT ? "input" : "output";
  gint index;

  g_return_if_fail (priv->props);

  index = find_device_route (device, name, device_id);
  if (index < 0) {
    wp_info_object (self, "route '%s' (%d) is not valid", name, index);
    return;
  }

  /* Get the device name */
  dev_name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NAME);
  g_return_if_fail (dev_name);

  /*
   * Property keys cannot contain spaces or brackets,
   * make sure we use a valid string
   */
  routes_name = g_strcanon (g_strdup (name), ALLOW_CHARS, '_');

  prop_name = g_strdup_printf ("%s:%s:%s:%s", dev_name, dir_str, routes_name,
      "volume");
  prop_value = wp_properties_get (priv->props, prop_name);
  g_free (prop_name);
  if (prop_value) {
    wp_spa_pod_builder_add_property (builder, "volume");
    wp_spa_pod_builder_add_float (builder, strtof (prop_value, NULL));
  }

  prop_name = g_strdup_printf ("%s:%s:%s:%s", dev_name, dir_str, routes_name,
      "mute");
  prop_value = wp_properties_get (priv->props, prop_name);
  g_free (prop_name);
  if (prop_value) {
    wp_spa_pod_builder_add_property (builder, "mute");
    wp_spa_pod_builder_add_boolean (builder,
        g_strcmp0 (prop_value, "true") == 0 ? TRUE : FALSE);
  }

  prop_name = g_strdup_printf ("%s:%s:%s:%s", dev_name, dir_str, routes_name,
      "channelVolumes");
  prop_value = wp_properties_get (priv->props, prop_name);
  g_free (prop_name);
  if (prop_value) {
    g_autoptr (GArray) values = parse_channel_volumes (prop_value);
    if (values) {
      g_autoptr (WpSpaPodBuilder) chvol_builder = wp_spa_pod_builder_new_array ();
      /* Build the channelVolumes array spa pod */
      for (guint i = 0; i < values->len; i++) {
        wp_spa_pod_builder_add_float (chvol_builder,
            g_array_index (values, float, i));
      }
      /* Add the channelVolumes property */
      wp_spa_pod_builder_add_property (builder, "channelVolumes");
      g_autoptr (WpSpaPod) chvol = wp_spa_pod_builder_end (chvol_builder);
      wp_spa_pod_builder_add_pod (builder, chvol);
    }
  }

  prop_name = g_strdup_printf ("%s:%s:%s:%s", dev_name, dir_str, routes_name,
      "channelMap");
  prop_value = wp_properties_get (priv->props, prop_name);
  g_free (prop_name);
  if (prop_value) {
    g_autoptr (GPtrArray) values = parse_channel_map (prop_value);
    if (values) {
      g_autoptr (WpSpaPodBuilder) chmap_builder = wp_spa_pod_builder_new_array ();
      /* Build the channelMap array spa pod */
      for (guint i = 0; i < values->len; i++) {
        gchar *channel = g_ptr_array_index (values, i);
        wp_spa_pod_builder_add_id (chmap_builder, channel_from_name (channel));
      }
      /* Add the channelMap property */
      wp_spa_pod_builder_add_property (builder, "channelMap");
      g_autoptr (WpSpaPod) chmap = wp_spa_pod_builder_end (chmap_builder);
      wp_spa_pod_builder_add_pod (builder, chmap);
    }
  }

  props = wp_spa_pod_builder_end (builder);

  wp_pipewire_object_set_param (device, "Route", 0,
      wp_spa_pod_new_object (
          "Spa:Pod:Object:Param:Route", "Route",
          "index", "i", index,
          "device", "i", device_id,
          "props", "O", props,
          NULL));
  wp_info_object (self, "properties set for route %d on " WP_OBJECT_FORMAT,
      index, WP_OBJECT_ARGS (device));
}

static void
save_routes_properties (WpDefaultRoutes *self, WpPipewireObject *device,
    const gchar *name, WpDirection direction, WpSpaPod *properties)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);
  const gchar *dev_name = NULL;
  gchar *routes_name = NULL;
  gchar *prop_name = NULL;
  gchar *prop_value = NULL;
  g_autoptr (WpIterator) props = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  gchar *dir_str = direction == WP_DIRECTION_INPUT ? "input" : "output";

  g_return_if_fail (properties);
  g_return_if_fail (priv->props);

  /* Get the device name */
  dev_name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NAME);
  g_return_if_fail (dev_name);

  routes_name = g_strcanon (g_strdup (name), ALLOW_CHARS, '_');

  props = wp_spa_pod_new_iterator (properties);
  for (; wp_iterator_next (props, &item); g_value_unset (&item)) {
    WpSpaPod *pod = g_value_get_boxed (&item);
    const char *p_key = NULL;
    g_autoptr (WpSpaPod) p_val = NULL;
    wp_spa_pod_get_property (pod, &p_key, &p_val);

    prop_name = g_strdup_printf ("%s:%s:%s:%s", dev_name, dir_str,
        routes_name, p_key);

    /* volume */
    if (g_strcmp0 (p_key, "volume") == 0) {
      float vol = 0.0f;
      wp_spa_pod_get_float (p_val, &vol);
      prop_value = g_strdup_printf ("%f", vol);
    }
    /* mute */
    else if (g_strcmp0 (p_key, "mute") == 0) {
      gboolean b = FALSE;
      wp_spa_pod_get_boolean (p_val, &b);
      prop_value = g_strdup (b ? "true" : "false");
    }
    /* channelVolumes */
    else if (g_strcmp0 (p_key, "channelVolumes") == 0) {
      g_autoptr (WpIterator) it2 = wp_spa_pod_new_iterator (p_val);
      g_auto (GValue) item2 = G_VALUE_INIT;
      guint i = 0, n_vols = 0;
      float vols[SPA_AUDIO_MAX_CHANNELS];

      for (; wp_iterator_next (it2, &item2); g_value_unset (&item2)) {
        float *vol = (float *) g_value_get_pointer (&item2);
        vols[n_vols] = *vol;
        n_vols++;
      }
      if (n_vols > 0) {
        size_t size;
        FILE *f;

        f = open_memstream (&prop_value, &size);
        fprintf (f, "[ ");
        for (i = 0; i < n_vols; i++)
          fprintf (f, "%s%f", i > 0 ? ", " : "", vols[i]);
        fprintf (f, " ]");
        fclose (f);
      }
    }
    /* channelMap */
    else if (g_strcmp0 (p_key, "channelMap") == 0) {
      g_autoptr (WpIterator) it2 = wp_spa_pod_new_iterator (p_val);
      g_auto (GValue) item2 = G_VALUE_INIT;
      guint i = 0, n_vals = 0;
      guint vals[SPA_AUDIO_MAX_CHANNELS];

      for (; wp_iterator_next (it2, &item2); g_value_unset (&item2)) {
        guint *val = (guint *) g_value_get_pointer (&item2);
        vals[n_vals] = *val;
        n_vals++;
      }
      if (n_vals > 0) {
        size_t size;
        FILE *f;

        f = open_memstream (&prop_value, &size);
        fprintf (f, "[ ");
        for (i = 0; i < n_vals; i++)
          fprintf (f, "%s\"%s\"", i > 0 ? ", " : "", channel_to_name (vals[i]));
        fprintf (f, " ]");
        fclose (f);
      }
    }

    if (prop_value) {
      wp_properties_set (priv->props, prop_name, prop_value);
      g_free (prop_value);
    }
    if (prop_name)
      g_free (prop_name);

    prop_name = prop_value = NULL;
  }
  g_free (routes_name);
  timeout_save_properties (self, SAVE_INTERVAL_MS);
}

static gchar *
serialize_routes (GHashTable *routes)
{
  GHashTableIter iter;
  const gchar *key;
  gpointer value;
  gchar *ptr;
  FILE *f;
  size_t size;
  gboolean first = TRUE;

  g_return_val_if_fail (routes, NULL);
  g_return_val_if_fail (g_hash_table_size (routes), NULL);

  f = open_memstream (&ptr, &size);
  g_return_val_if_fail (f, NULL);

  /* Routes are stored in a JSON array */
  fprintf (f, "[ ");

  g_hash_table_iter_init (&iter, routes);
  while (g_hash_table_iter_next (&iter, (gpointer *) &key, &value)) {
    if (!first)
      fprintf (f, ", ");
    /* Each route is stored as a JSON object with "name" and "device" attributes */
    fprintf (f, "{ \"name\": \"%s\", \"device\": %d }", key, GPOINTER_TO_INT (value));
    first = FALSE;
  }
  fprintf (f, " ]");
  fclose (f);

  return ptr;
}

static GHashTable *
parse_routes (const gchar *routes_str)
{
  struct spa_json array;
  struct spa_json item;
  struct spa_json object;
  GHashTable *routes;

  spa_json_init (&array, routes_str, strlen (routes_str));
  if (spa_json_enter_array (&array, &item) <= 0)
    return NULL;

  routes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  while (spa_json_enter_object (&item, &object) > 0) {
    const char *prop;
    char name[MAX_JSON_STRING_LEN];
    int dev_id, res;

    dev_id = -1;
    name[0] = 0;

    while ((res = spa_json_next (&object, &prop)) > 0) {
      if (strncmp (prop, "\"name\"", res) == 0)
        res = spa_json_get_string (&object, name, MAX_JSON_STRING_LEN);
      else if (strncmp (prop, "\"device\"", res) == 0)
        res = spa_json_get_int (&object, &dev_id);

      if (res <= 0) {
        g_critical ("unable to parse route");
        g_hash_table_destroy (routes);
        return NULL;
      }
    }

    if (dev_id >= 0 && strlen(name) > 0)
      g_hash_table_insert (routes, g_strdup(name), GINT_TO_POINTER (dev_id));
  }

  return routes;
}

static void
hash_table_copy_elements (gpointer key, gpointer value, gpointer data)
{
  GHashTable *ht = data;
  gchar *route = key;

  g_hash_table_insert (ht, g_strdup(route), value);
}

static gboolean
timeout_save_routes_cb (WpDefaultRoutes *self)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);
  WpPipewireObject *device;
  GHashTableIter iter;
  GHashTable *ht;

  /*
   * `default_routes` is the reference list, used when other modules require
   * the default routes for a given device.
   * `current_routes` is a working copy, which is copied into `default_routes`
   * when saving to disk.
   */
  g_hash_table_remove_all (priv->default_routes);

  g_hash_table_iter_init (&iter, priv->current_routes);
  while (g_hash_table_iter_next (&iter, (gpointer*) &device, (gpointer*) &ht)) {
    const gchar *dev_name;
    gchar *routes;
    GHashTable *copy = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        NULL);

    dev_name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NAME);
    if (!dev_name)
      continue;

    routes = serialize_routes (ht);
    if (routes) {
      wp_properties_set (priv->routes, dev_name, routes);
      g_free (routes);
    }

    g_hash_table_foreach (ht, hash_table_copy_elements, copy);
    g_hash_table_insert (priv->default_routes, device, copy);
  }

  if (!wp_state_save (priv->state, "routes", priv->routes))
    wp_warning_object (self, "could not save routes");

  return G_SOURCE_REMOVE;
}

static void
timeout_save_routes (WpDefaultRoutes *self, guint ms)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (self));

  g_return_if_fail (core);
  g_return_if_fail (priv->routes);

  /* Clear the current timeout callback */
  if (priv->routes_timeout)
      g_source_destroy (priv->routes_timeout);
  g_clear_pointer (&priv->routes_timeout, g_source_unref);

  /* Add the timeout callback */
  wp_core_timeout_add_closure (core, &priv->routes_timeout, ms,
      g_cclosure_new_object (G_CALLBACK (timeout_save_routes_cb),
      G_OBJECT (self)));
}

static void
wp_default_routes_get_routes (WpDefaultRoutes *self, WpPipewireObject *device,
    GHashTable **routes)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);

  g_return_if_fail (device);
  g_return_if_fail (routes);
  g_return_if_fail (priv->default_routes);

  *routes = g_hash_table_lookup (priv->default_routes, device);
}

static void
update_routes (WpDefaultRoutes *self, WpPipewireObject *device,
    GHashTable *new_routes)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);
  GHashTable *curr_routes;
  GHashTableIter iter;

  g_return_if_fail (new_routes);
  g_return_if_fail (priv->routes);

  curr_routes = g_hash_table_lookup (priv->current_routes, device);

  /* Check if the new routes are the same as the current ones */
  if (curr_routes) {
    if (g_hash_table_size (curr_routes) == g_hash_table_size (new_routes)) {
      gboolean identical = TRUE;
      const gchar *key;
      gpointer value;
      gint index;

      g_hash_table_iter_init (&iter, new_routes);
      while (g_hash_table_iter_next (&iter, (gpointer*) &key, &value)) {
        /* Make sure the route is valid */
        index = find_device_route (device, key, GPOINTER_TO_INT (value));
        if (index < 0) {
          wp_info_object (self, "route '%s' (%d) is not valid", key, index);
          return;
        }
        if (!g_hash_table_contains (curr_routes, key)) {
          identical = FALSE;
          break;
        }
      }
      if (identical)
        return;
    }
  }

  /* Otherwise update the route and add timeout save callback */
  g_hash_table_insert (priv->current_routes, device, new_routes);
  timeout_save_routes (self, SAVE_INTERVAL_MS);
}

static void
on_device_routes_notified (WpPipewireObject *device, GAsyncResult *res,
    WpDefaultRoutes *self)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);
  g_autoptr (WpIterator) routes = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  const gchar *name = NULL;
  gint device_id, direction = 0;
  GHashTable *new_routes;
  GHashTable *ht;

  /* Finish */
  routes = wp_pipewire_object_enum_params_finish (device, res, &error);
  if (error) {
    wp_warning_object (self, "failed to get current route on device");
    return;
  }

  new_routes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  if (!new_routes) {
    wp_warning_object (self, "failed to allocate new hash table");
    return;
  }

  ht = g_hash_table_lookup (priv->current_routes, device);

  for (; wp_iterator_next (routes, &item); g_value_unset (&item)) {
    /* Parse the route */
    WpSpaPod *pod = g_value_get_boxed (&item);
    WpSpaPod *props;
    if (!wp_spa_pod_get_object (pod, NULL,
        "direction", "I", &direction,
        "device", "i", &device_id,
        "name", "s", &name,
        "props", "P", &props,
        NULL)) {
      wp_warning_object (self, "failed to parse current route");
      continue;
    }

    if (ht) {
      if (g_hash_table_contains (ht, name))
        save_routes_properties (self, device, name, direction, props);
      else if (g_hash_table_size (ht) > 0)
        /*
         * Apply route properties only once we have filled the `current_routes`
         * table for this device. This prevents race conditions when a device
         * appears.
         */
        apply_routes_properties (self, device, name, direction, device_id);
    }

    g_hash_table_insert (new_routes, g_strdup (name),
        GINT_TO_POINTER (device_id));
  }

  /* Update the routes list */
  update_routes (self, device, new_routes);
}

static void
on_device_param_info_notified (WpPipewireObject * device, GParamSpec * param,
    WpDefaultRoutes *self)
{
  /* Check the route every time the params have changed */
  wp_pipewire_object_enum_params (device, "Route", NULL, NULL,
      (GAsyncReadyCallback) on_device_routes_notified, self);
}

static void
on_device_enum_routes_done (WpPipewireObject *device, GAsyncResult *res,
    WpDefaultRoutes *self)
{
  g_autoptr (WpIterator) routes = NULL;
  g_autoptr (GError) error = NULL;

  /* Finish */
  routes = wp_pipewire_object_enum_params_finish (device, res, &error);
  if (error) {
    wp_warning_object (self, "failed to enum routes in device "
        WP_OBJECT_FORMAT, WP_OBJECT_ARGS (device));
    return;
  }

  /* Keep a reference of the routes in the device object */
  g_object_set_qdata_full (G_OBJECT (device), routes_quark (),
        g_steal_pointer (&routes), (GDestroyNotify) wp_iterator_unref);

  /* Watch for param info changes */
  g_signal_connect_object (device, "notify::param-info",
      G_CALLBACK (on_device_param_info_notified), self, 0);
}

static void
on_device_added (WpObjectManager *om, WpPipewireObject *device, gpointer d)
{
  WpDefaultRoutes *self = WP_DEFAULT_ROUTES (d);
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);
  const gchar *dev_name = NULL;
  const gchar *routes = NULL;
  GHashTable *ht;

  wp_warning_object (self, "device " WP_OBJECT_FORMAT " added",
      WP_OBJECT_ARGS (device));

  /* Enum available routes */
  wp_pipewire_object_enum_params (device, "EnumRoute", NULL, NULL,
      (GAsyncReadyCallback) on_device_enum_routes_done, self);

  /* Load default routes for device */
  dev_name = wp_pipewire_object_get_property (device, PW_KEY_DEVICE_NAME);
  g_return_if_fail (dev_name);

  routes = wp_properties_get (priv->routes, dev_name);
  g_return_if_fail (routes);

  ht = parse_routes (routes);
  g_return_if_fail (ht);

  g_hash_table_insert (priv->default_routes, device, ht);
}

static void
on_device_removed (WpObjectManager *om, WpPipewireObject *device, gpointer d)
{
  WpDefaultRoutes *self = WP_DEFAULT_ROUTES (d);
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);

  wp_warning_object (self, "device " WP_OBJECT_FORMAT " removed",
      WP_OBJECT_ARGS (device));

  g_hash_table_remove (priv->current_routes, device);
  g_hash_table_remove (priv->default_routes, device);
}

static void
wp_default_routes_enable (WpPlugin * plugin, WpTransition * transition)
{
  g_autoptr (WpCore) core = wp_object_get_core (WP_OBJECT (plugin));
  WpDefaultRoutes *self = WP_DEFAULT_ROUTES (plugin);
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);

  /* Create the devices object manager */
  priv->devices_om = wp_object_manager_new ();
  wp_object_manager_add_interest (priv->devices_om, WP_TYPE_DEVICE, NULL);
  wp_object_manager_request_object_features (priv->devices_om,
      WP_TYPE_DEVICE, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
  g_signal_connect_object (priv->devices_om, "object-added",
      G_CALLBACK (on_device_added), self, 0);
  g_signal_connect_object (priv->devices_om, "object-removed",
      G_CALLBACK (on_device_removed), self, 0);
  wp_core_install_object_manager (core, priv->devices_om);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_default_routes_disable (WpPlugin * plugin)
{
  WpDefaultRoutes *self = WP_DEFAULT_ROUTES (plugin);
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);

  g_clear_object (&priv->devices_om);
}

static void
wp_default_routes_finalize (GObject * object)
{
  WpDefaultRoutes *self = WP_DEFAULT_ROUTES (object);
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);

  /* Clear the current timeout callback */
  if (priv->props_timeout)
    g_source_destroy (priv->props_timeout);
  g_clear_pointer (&priv->props_timeout, g_source_unref);
  if (priv->routes_timeout)
    g_source_destroy (priv->routes_timeout);
  g_clear_pointer (&priv->routes_timeout, g_source_unref);
  g_hash_table_destroy (priv->current_routes);
  g_hash_table_destroy (priv->default_routes);
  g_clear_pointer (&priv->routes, wp_properties_unref);
  g_clear_object (&priv->state);
}

static void
wp_default_routes_init (WpDefaultRoutes * self)
{
  WpDefaultRoutesPrivate *priv = wp_default_routes_get_instance_private (self);

  priv->state = wp_state_new (STATE_NAME);

  wp_warning_object (self, "module default route loaded");

  priv->current_routes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_hash_table_destroy);
  priv->default_routes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) g_hash_table_destroy);

  /* Load the saved routes */
  priv->routes = wp_state_load (priv->state, "routes");
  if (!priv->routes) {
    wp_warning_object (self, "could not load routes");
  }
  /* Load the saved properties */
  priv->props = wp_state_load (priv->state, "properties");
  if (!priv->props) {
    wp_warning_object (self, "could not load properties");
  }
}

static void
wp_default_routes_class_init (WpDefaultRoutesClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  object_class->finalize = wp_default_routes_finalize;
  plugin_class->enable = wp_default_routes_enable;
  plugin_class->disable = wp_default_routes_disable;

  klass->get_routes = wp_default_routes_get_routes;

  /* Signals */
  signals[SIGNAL_GET_ROUTES] = g_signal_new ("get-routes",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (WpDefaultRoutesClass, get_routes), NULL, NULL,
      NULL, G_TYPE_NONE, 2, WP_TYPE_DEVICE, G_TYPE_POINTER);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_default_routes_get_type (),
      "name", STATE_NAME,
      "core", core,
      NULL));
  return TRUE;
}
