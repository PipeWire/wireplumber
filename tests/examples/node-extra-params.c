/* WirePlumber
 *
 * Copyright © 2024 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * This is an example that shows how to correctly set additional node properties
 * that reside in a special "Props" field that is called "params". These show up
 * in pw-dump as an array, like this:
 *
 *  "Props": [
 *    {
 *      ...
 *      "params": [
 *        "key1",
 *        value1,
 *        "key2",
 *        value2,
 *        ...
 *      ]
 *    },
 *    {
 *      "params": [
 *        "additional_key",
 *        additional_value,
 *      ]
 *    }
 *  ],
 *
 * The correct way to set them is to construct a Props object that has a "params"
 * property and inside that property add a POD structure with the key/value
 * pairs listed in order.
 *
 * This example also parses the key/value pairs from a JSON array that is provided
 * on the command line, so you can call this like this:
 *
 *  $ ./filter-chain-params NODE_ID '["key1", value1, "key2", value2]'
 *
 */

#include <wp/wp.h>
#include <stdio.h>

typedef struct
{
  GMainLoop *loop;
  WpCore *core;
  WpObjectManager *om;

  gint arg_id;
  WpSpaJson *arg_params;
} Data;

static void
async_quit (WpCore * core, GAsyncResult * res, Data * data)
{
  g_main_loop_quit (data->loop);
}

static WpSpaPod *
construct_params_pod (WpSpaJson * params_j)
{
  // the inner POD is a struct
  g_autoptr (WpSpaPodBuilder) b_struct = wp_spa_pod_builder_new_struct ();

  // iterate the JSON array and fill the inner POD
  int state = 0;
  g_autoptr (WpIterator) it = wp_spa_json_new_iterator (params_j);
  g_auto (GValue) val = G_VALUE_INIT;

  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpSpaJson *val_json = g_value_get_boxed (&val);
    switch (state) {
      case 0: { //parsing key
        g_autofree gchar * key = wp_spa_json_parse_string (val_json);
        wp_spa_pod_builder_add_string (b_struct, key);
        break;
      }
      case 1: //parsing value
        if (wp_spa_json_is_int (val_json)) {
          gint value = 0;
          wp_spa_json_parse_int (val_json, &value);
          wp_spa_pod_builder_add_int (b_struct, value);
        }
        else if (wp_spa_json_is_float (val_json)) {
          gfloat value = 0.0;
          wp_spa_json_parse_float (val_json, &value);
          wp_spa_pod_builder_add_float (b_struct, value);
        }
        else if (wp_spa_json_is_boolean (val_json)) {
          gboolean value = FALSE;
          wp_spa_json_parse_boolean (val_json, &value);
          wp_spa_pod_builder_add_boolean (b_struct, value);
        }
        else {
          g_autofree gchar * value = wp_spa_json_parse_string (val_json);
          wp_spa_pod_builder_add_string (b_struct, value);
        }
        break;
      default:
        break;
    }
    state = (state + 1) % 2;
  }

  if (state == 1)
    printf ("WARNING: last key didn't have a value!\n");

  g_autoptr (WpSpaPod) pod_struct = wp_spa_pod_builder_end (b_struct);

  // now fill the outer POD, which is an object of type Props
  g_autoptr (WpSpaPodBuilder) b_obj =
      wp_spa_pod_builder_new_object ("Spa:Pod:Object:Param:Props", "Props");
  wp_spa_pod_builder_add (b_obj, "params", "P", pod_struct, NULL);

  return wp_spa_pod_builder_end (b_obj);
}

static void
on_om_installed (WpObjectManager * om, Data * data)
{
  g_autoptr (WpPipewireObject) pwobj = wp_object_manager_lookup (om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", data->arg_id, NULL);

  wp_pipewire_object_set_param (pwobj, "Props", 0,
      construct_params_pod (data->arg_params));

  wp_core_sync (data->core, NULL, (GAsyncReadyCallback) async_quit, data);
}

static void
on_core_activated (WpObject * core, GAsyncResult * res, Data * data)
{
  g_autoptr (GError) error = NULL;

  if (!wp_object_activate_finish (core, res, &error)) {
    fprintf (stderr, "%s\n", error->message);
    g_main_loop_quit (data->loop);
    return;
  }

  data->om = wp_object_manager_new ();
  wp_object_manager_add_interest (data->om, WP_TYPE_NODE,
      WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", data->arg_id, NULL);
  wp_object_manager_request_object_features (data->om, WP_TYPE_NODE,
      WP_PIPEWIRE_OBJECT_FEATURES_ALL);
  g_signal_connect (data->om, "installed", G_CALLBACK (on_om_installed), data);
  wp_core_install_object_manager (data->core, data->om);
}

int
main (int argc, char **argv)
{
  Data data = {0};

  wp_init (WP_INIT_ALL);

  if (argc < 2) {
    printf ("Usage: %s ID '[param1, value1, param2, value2, ...]'\n", argv[0]);
    return 1;
  }

  data.loop = g_main_loop_new (NULL, FALSE);
  data.core = wp_core_new (NULL, NULL, NULL);

  data.arg_id = atoi (argv[1]);
  data.arg_params = wp_spa_json_new_wrap_string (argv[2]);

  wp_object_activate (WP_OBJECT (data.core), WP_CORE_FEATURE_CONNECTED, NULL,
      (GAsyncReadyCallback) on_core_activated, &data);

  g_main_loop_run (data.loop);

  wp_spa_json_unref (data.arg_params);
  g_object_unref (data.core);
  g_main_loop_unref (data.loop);
  return 0;
}
