/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <wp/wp.h>

#include <spa/utils/defs.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

#include <pipewire/pipewire.h>
#include <pipewire/extensions/endpoint.h>
#include <pipewire/extensions/client-endpoint.h>

G_DEFINE_QUARK (module-pipewire-remote-endpoint-data, remote_endpoint_data);

struct proxy_priv
{
  struct spa_hook proxy_listener;
  struct spa_hook cli_ep_listener;
};

static const struct spa_param_info static_param_info[] = {
  SPA_PARAM_INFO (PW_ENDPOINT_PARAM_EnumControl, SPA_PARAM_INFO_READ),
  SPA_PARAM_INFO (PW_ENDPOINT_PARAM_Control, SPA_PARAM_INFO_READWRITE),
  SPA_PARAM_INFO (PW_ENDPOINT_PARAM_EnumStream, SPA_PARAM_INFO_READ)
};

static struct spa_pod *
control_to_pod (GVariant * control, guint32 * control_id,
    struct spa_pod_builder *b)
{
  struct spa_pod_frame f;
  guint32 id, stream_id;
  const gchar *name;
  const gchar *type;

  if (!g_variant_lookup (control, "id", "u", &id) ||
      !g_variant_lookup (control, "stream-id", "u", &stream_id) ||
      !g_variant_lookup (control, "name", "&s", &name) ||
      !g_variant_lookup (control, "type", "&s", &type) ||
      !g_variant_type_string_is_valid (type))
  {
    g_autofree gchar *dump = g_variant_print (control, TRUE);
    g_warning ("invalid endpoint control GVariant: %s", dump);
    return NULL;
  }

  spa_pod_builder_push_object (b, &f,
      PW_ENDPOINT_OBJECT_ParamControl, PW_ENDPOINT_PARAM_EnumControl);
  spa_pod_builder_add (b,
      PW_ENDPOINT_PARAM_CONTROL_id, SPA_POD_Id (id),
      PW_ENDPOINT_PARAM_CONTROL_stream_id, SPA_POD_Id (stream_id),
      PW_ENDPOINT_PARAM_CONTROL_name, SPA_POD_String (name),
      NULL);

  switch (type[0]) {
    case 'b':
    {
      gboolean def = false;
      g_variant_lookup (control, "default-value", "b", &def);
      spa_pod_builder_add (b,
          PW_ENDPOINT_PARAM_CONTROL_type, SPA_POD_CHOICE_Bool (def),
          NULL);
      break;
    }
    case 'd':
    {
      gdouble def = 0.0, min = -G_MAXDOUBLE, max = G_MAXDOUBLE;
      g_variant_lookup (control, "default-value", "d", &def);
      g_variant_lookup (control, "range", "(dd)", &min, &max);
      spa_pod_builder_add (b,
          PW_ENDPOINT_PARAM_CONTROL_type,
          SPA_POD_CHOICE_RANGE_Double (def, min, max),
          NULL);
      break;
    }
    case 'i':
    {
      gint32 def = 0, min = G_MININT32, max = G_MAXINT32;
      g_variant_lookup (control, "default-value", "i", &def);
      g_variant_lookup (control, "range", "(ii)", &min, &max);
      spa_pod_builder_add (b,
          PW_ENDPOINT_PARAM_CONTROL_type,
          SPA_POD_CHOICE_RANGE_Int (def, min, max),
          NULL);
      break;
    }
    case 'x':
    {
      gint64 def = 0, min = G_MININT64, max = G_MAXINT64;
      g_variant_lookup (control, "default-value", "x", &def);
      g_variant_lookup (control, "range", "(xx)", &min, &max);
      spa_pod_builder_add (b,
          PW_ENDPOINT_PARAM_CONTROL_type,
          SPA_POD_CHOICE_RANGE_Long (def, min, max),
          NULL);
      break;
    }
    default:
      g_warning ("invalid type '%s' for endpoint control value", type);
      break;
  }

  *control_id = id;
  return spa_pod_builder_pop (b, &f);
}

static struct spa_pod *
control_value_to_pod (GVariant * value, guint32 id, struct spa_pod_builder *b)
{
  struct spa_pod_frame f;
  const GVariantType *type;

  spa_pod_builder_push_object (b, &f,
      PW_ENDPOINT_OBJECT_ParamControl, PW_ENDPOINT_PARAM_Control);
  spa_pod_builder_add (b,
      PW_ENDPOINT_PARAM_CONTROL_id, SPA_POD_Id (id),
      NULL);

  type = g_variant_get_type (value);

  switch (g_variant_type_peek_string (type)[0]) {
    case 'b':
    {
      gboolean val = g_variant_get_boolean (value);
      spa_pod_builder_add (b,
          PW_ENDPOINT_PARAM_CONTROL_value, SPA_POD_Bool (val),
          NULL);
      break;
    }
    case 'd':
    {
      gdouble val = g_variant_get_double (value);
      spa_pod_builder_add (b,
          PW_ENDPOINT_PARAM_CONTROL_value, SPA_POD_Double (val),
          NULL);
      break;
    }
    case 'i':
    {
      gint32 val = g_variant_get_int32 (value);
      spa_pod_builder_add (b,
          PW_ENDPOINT_PARAM_CONTROL_value, SPA_POD_Int (val),
          NULL);
      break;
    }
    case 'x':
    {
      gint64 val = g_variant_get_int64 (value);
      spa_pod_builder_add (b,
          PW_ENDPOINT_PARAM_CONTROL_value, SPA_POD_Long (val),
          NULL);
      break;
    }
    default:
    {
      g_autofree gchar * type_string = g_variant_type_dup_string (type);
      g_warning ("invalid type '%s' for endpoint control value", type_string);
      break;
    }
  }

  return spa_pod_builder_pop (b, &f);
}

static struct spa_pod *
stream_to_pod (GVariant * control, struct spa_pod_builder *b)
{
  struct spa_pod_frame f;
  guint32 id;
  const gchar *name;

  if (!g_variant_lookup (control, "id", "u", &id) ||
      !g_variant_lookup (control, "name", "&s", &name))
  {
    g_autofree gchar *dump = g_variant_print (control, TRUE);
    g_warning ("invalid endpoint stream GVariant: %s", dump);
    return NULL;
  }

  spa_pod_builder_push_object (b, &f,
      PW_ENDPOINT_OBJECT_ParamStream, PW_ENDPOINT_PARAM_EnumStream);
  spa_pod_builder_add (b,
      PW_ENDPOINT_PARAM_STREAM_id, SPA_POD_Id (id),
      PW_ENDPOINT_PARAM_STREAM_name, SPA_POD_String (name),
      NULL);
  return spa_pod_builder_pop (b, &f);
}

static void
endpoint_update (WpEndpoint *ep,
    struct pw_client_endpoint_proxy *client_ep_proxy)
{
  guint8 buffer[8192];
  struct spa_pod_builder b;
  guint32 change_mask = 0;
  guint32 n_params, n_controls, n_streams;
  g_autoptr (GVariant) controls, streams;
  const struct spa_pod **params = NULL;
  const struct spa_pod **tmp_ctl_params = NULL;
  guint32 i, index = 0;

  controls = wp_endpoint_list_controls (ep);
  n_controls = g_variant_n_children (controls);
  streams = wp_endpoint_list_streams (ep);
  n_streams = g_variant_n_children (streams);

  n_params = 2 * n_controls + n_streams;
  if (n_params == 0)
    goto action;

  params = g_alloca ((n_params + n_controls) * sizeof (struct spa_pod *));
  tmp_ctl_params = params + n_params;

  spa_pod_builder_init (&b, &buffer, sizeof (buffer));

  for (i = 0; i < n_controls; i++) {
    g_autoptr (GVariant) control = NULL;
    g_autoptr (GVariant) value = NULL;
    guint32 control_id;

    control = g_variant_get_child_value (controls, i);
    tmp_ctl_params[index] = control_to_pod (control, &control_id, &b);
    if (!tmp_ctl_params[index])
      continue;

    value = wp_endpoint_get_control_value (ep, control_id);
    params[index] = control_value_to_pod (value, control_id, &b);
    if (!params[index])
      continue;

    index++;
  }

  if (index > 0) {
    memcpy (&params[index], tmp_ctl_params, index * sizeof (struct spa_pod *));
    index *= 2;
  }

  for (i = 0; i < n_streams; i++) {
    g_autoptr (GVariant) stream = NULL;

    stream = g_variant_get_child_value (streams, i);
    params[index] = stream_to_pod (stream, &b);
    if (!params[index])
      continue;

    index++;
  }

  n_params = index;
  change_mask |= PW_CLIENT_ENDPOINT_UPDATE_PARAMS;

action:
  pw_client_endpoint_proxy_update (client_ep_proxy,
      change_mask | PW_CLIENT_ENDPOINT_UPDATE_PARAM_INFO,
      n_params, params,
      SPA_N_ELEMENTS (static_param_info), static_param_info,
      NULL);
}

static void
on_endpoint_notify_control_value (WpEndpoint * ep, guint32 control_id,
    struct pw_client_endpoint_proxy *client_ep_proxy)
{
  guint8 buffer[1024];
  struct spa_pod_builder b;
  g_autoptr (GVariant) value = NULL;
  const struct spa_pod *params[1];

  //FIXME: the signal should come with the value as argument,
  // there is no point in re-acquiring it in every signal handler
  value = wp_endpoint_get_control_value (ep, control_id);

  spa_pod_builder_init (&b, buffer, sizeof (buffer));
  params[0] = control_value_to_pod (value, control_id, &b);

  pw_client_endpoint_proxy_update (client_ep_proxy,
      PW_CLIENT_ENDPOINT_UPDATE_PARAMS_INCREMENTAL,
      1, params, 0, NULL, NULL);
}

static void
client_endpoint_set_param (void *object, uint32_t id, uint32_t flags,
    const struct spa_pod *param)
{
  WpEndpoint *ep = object;
  struct pw_proxy *proxy;
  int res = 0;
  struct spa_pod_parser p;
  struct spa_pod_frame f;
  guint32 obj_id, control_id;
  struct spa_pod *value;
  GVariant *variant;

  if (id != PW_ENDPOINT_PARAM_Control) {
    res = -EINVAL;
    goto error;
  }

  spa_pod_parser_pod (&p, param);

  if ((res = spa_pod_parser_push_object (&p, &f,
          PW_ENDPOINT_OBJECT_ParamControl, &obj_id)) < 0)
    goto error;

  if (obj_id != PW_ENDPOINT_PARAM_Control) {
    res = -EPROTO;
    goto error;
  }

  if ((res = spa_pod_parser_get (&p,
          PW_ENDPOINT_PARAM_CONTROL_id, SPA_POD_Id (&control_id),
          PW_ENDPOINT_PARAM_CONTROL_value, SPA_POD_Pod (&value),
          NULL)) < 0)
    goto error;

  switch (SPA_POD_TYPE (value)) {
    case SPA_TYPE_Bool:
    {
      bool v;
      if ((res = spa_pod_get_bool (value, &v)) < 0)
        goto error;
      variant = g_variant_new_boolean (v);
      break;
    }
    case SPA_TYPE_Int:
    {
      gint32 v;
      if ((res = spa_pod_get_int (value, &v)) < 0)
        goto error;
      variant = g_variant_new_int32 (v);
      break;
    }
    case SPA_TYPE_Long:
    {
      gint64 v;
      if ((res = spa_pod_get_long (value, &v)) < 0)
        goto error;
      variant = g_variant_new_int64 (v);
      break;
    }
    case SPA_TYPE_Double:
    {
      gdouble v;
      if ((res = spa_pod_get_double (value, &v)) < 0)
        goto error;
      variant = g_variant_new_double (v);
      break;
    }
    default:
      res = -EPROTO;
      goto error;
  }

  wp_endpoint_set_control_value (ep, control_id, variant);
  return;

error:
  proxy = g_object_get_qdata (G_OBJECT (ep), remote_endpoint_data_quark ());
  g_warning ("set_param: bad arguments");
  pw_proxy_error (proxy, res, "set_param: bad arguments");
}

static const struct pw_client_endpoint_proxy_events client_endpoint_events = {
  PW_VERSION_CLIENT_ENDPOINT_PROXY_EVENTS,
  .set_param = client_endpoint_set_param,
};

static void
client_endpoint_proxy_destroy (void *object)
{
  WpEndpoint *ep = object;
  g_object_set_qdata (G_OBJECT (ep), remote_endpoint_data_quark (), NULL);
}

static const struct pw_proxy_events proxy_events = {
  PW_VERSION_PROXY_EVENTS,
  .destroy = client_endpoint_proxy_destroy,
};

static void
endpoint_added (WpCore *core, GQuark key, WpEndpoint *ep,
    struct pw_remote * remote)
{
  struct pw_core_proxy *core_proxy;
  struct pw_client_endpoint_proxy *client_ep_proxy;
  struct spa_dict_item props[] = {
    { "media.name", wp_endpoint_get_name (ep) },
    { "media.class", wp_endpoint_get_media_class (ep) }
  };
  struct spa_dict props_dict = SPA_DICT_INIT(props, SPA_N_ELEMENTS (props));
  struct proxy_priv *priv;

  g_return_if_fail (key == WP_GLOBAL_ENDPOINT);

  core_proxy = pw_remote_get_core_proxy (remote);
  client_ep_proxy = pw_core_proxy_create_object (core_proxy,
      "client-endpoint",
      PW_TYPE_INTERFACE_ClientEndpoint,
      PW_VERSION_CLIENT_ENDPOINT,
      &props_dict, sizeof (*priv));

  g_object_set_qdata (G_OBJECT (ep), remote_endpoint_data_quark (),
      client_ep_proxy);

  priv = pw_proxy_get_user_data ((struct pw_proxy *) client_ep_proxy);
  pw_proxy_add_listener ((struct pw_proxy *) client_ep_proxy,
      &priv->proxy_listener, &proxy_events, ep);
  pw_client_endpoint_proxy_add_listener (client_ep_proxy,
      &priv->cli_ep_listener, &client_endpoint_events, ep);

  endpoint_update (ep, client_ep_proxy);

  g_signal_connect (ep, "notify-control-value",
      (GCallback) on_endpoint_notify_control_value, client_ep_proxy);
}

static void
endpoint_removed (WpCore *sm, GQuark key, WpEndpoint *ep, gpointer _unused)
{
  struct pw_proxy *p;

  g_return_if_fail (key == WP_GLOBAL_ENDPOINT);

  p = g_object_get_qdata (G_OBJECT (ep), remote_endpoint_data_quark ());
  if (p)
    pw_proxy_destroy (p);
}

void
remote_endpoint_init (WpCore * core, struct pw_core *pw_core,
    struct pw_remote * remote)
{
  pw_module_load (pw_core, "libpipewire-module-endpoint", NULL, NULL, NULL,
      NULL);

  g_signal_connect (core, "global-added::endpoint",
      (GCallback) endpoint_added, remote);
  g_signal_connect (core, "global-removed::endpoint",
      (GCallback) endpoint_removed, NULL);
}
