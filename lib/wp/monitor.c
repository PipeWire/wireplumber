/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <spa/monitor/device.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/result.h>
#include <pipewire/pipewire.h>

#include "proxy-node.h"
#include "proxy-device.h"
#include "monitor.h"
#include "error.h"
#include "wpenums.h"
#include "private.h"

typedef struct {
  struct spa_handle *handle;
  struct spa_device *interface;
  struct spa_hook listener;
} WpSpaObject;

struct _WpMonitor
{
  GObject parent;

  /* Props */
  GWeakRef core;
  gchar *factory_name;
  WpProperties *properties;
  WpMonitorFlags flags;

  struct object *device;
};

struct object
{
  guint32 id;
  GType type;

  WpProxy *proxy;
  WpProperties *properties;

  GList *children;  /* element-type: struct object* */

  WpMonitor *self;
  WpSpaObject *spa_obj;
};

enum {
  PROP_0,
  PROP_CORE,
  PROP_FACTORY_NAME,
  PROP_PROPERTIES,
  PROP_FLAGS,
};

enum {
  SIG_SETUP_NODE_PROPS,
  SIG_SETUP_DEVICE_PROPS,
  N_SIGNALS
};

static guint32 signals[N_SIGNALS] = {0};

G_DEFINE_TYPE (WpMonitor, wp_monitor, G_TYPE_OBJECT)

static gpointer find_object (GList *list, guint32 id, GList **link);
static struct object * node_new (struct object *dev, uint32_t id,
    const struct spa_device_object_info *info);
static struct object * device_new (WpMonitor *self, uint32_t id,
    const gchar *factory_name, WpProperties *properties, GError **error);
static void object_free (struct object *obj);

/* device events */

static void
device_info (void *data, const struct spa_device_info *info)
{
  struct object *obj = data;

  /*
   * This is emited syncrhonously at the time we add the listener and
   * before object_info is emited. It gives us additional properties
   * about the device, like the "api.alsa.card.*" ones that are not
   * set by the monitor
   */
  if (info->change_mask & SPA_DEVICE_CHANGE_MASK_PROPS && obj->properties)
    wp_properties_update_from_dict (obj->properties, info->props);
}

static void
device_object_info (void *data, uint32_t id,
    const struct spa_device_object_info *info)
{
  struct object *obj = data;
  struct object *child = NULL;
  WpMonitor *self = obj->self;
  GList *link = NULL;
  g_autoptr (GError) err = NULL;

  /* Find the child */
  child = find_object (obj->children, id, &link);

  /* new object, construct... */
  if (info && !child) {
    /* Device */
    if (g_strcmp0 (info->type, SPA_TYPE_INTERFACE_Device) == 0) {
      if (!(child = device_new (self, id, info->factory_name,
              wp_properties_new_wrap_dict (info->props), &err)))
        g_debug ("WpMonitor:%p:%s %s", self, self->factory_name, err->message);
      return;
    }
    /* Node */
    else if (g_strcmp0 (info->type, SPA_TYPE_INTERFACE_Node) == 0) {
      if (!(child = node_new (obj, id, info)))
        return;
    }
    /* Default */
    else {
      g_debug ("WpMonitor:%p:%s got device_object_info for unknown object "
          "type %s", self, self->factory_name, info->type);
      return;
    }
    obj->children = g_list_append (obj->children, child);
  }
  /* object removed, delete... */
  else if (!info && child) {
    object_free (child);
    obj->children = g_list_delete_link (obj->children, link);
  }
}

static const struct spa_device_events device_events = {
  SPA_VERSION_DEVICE_EVENTS,
  .info = device_info,
  .object_info = device_object_info
};

/* WpSpaObject */

static void
wp_spa_object_free (WpSpaObject *self)
{
  spa_hook_remove (&self->listener);
  pw_unload_spa_handle (self->handle);
}

static inline WpSpaObject *
wp_spa_object_ref (WpSpaObject *self)
{
  return g_rc_box_acquire (self);
}

static inline void
wp_spa_object_unref (WpSpaObject *self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_spa_object_free);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpSpaObject, wp_spa_object_unref)

static WpSpaObject *
load_spa_object (WpCore *core, const gchar *factory, const char * iface_type,
    WpProperties *props, GError **error)
{
  g_autoptr (WpSpaObject) self = g_rc_box_new0 (WpSpaObject);
  gint res;

  /* Load the monitor handle */
  self->handle = pw_context_load_spa_handle (wp_core_get_pw_context (core),
      factory, props ? wp_properties_peek_dict (props) : NULL);
  if (!self->handle) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "SPA handle '%s' could not be loaded; is it installed?",
        factory);
    return NULL;
  }

  /* Get the handle interface */
  res = spa_handle_get_interface (self->handle, iface_type,
      (gpointer *)&self->interface);
  if (res < 0) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "Could not get interface %s from SPA handle", iface_type);
    return NULL;
  }

  return g_steal_pointer (&self);
}

/* struct object */

static gpointer
find_object (GList *list, guint32 id, GList **link)
{
  /*
   * The first element of struct object is the guint32 containing the id,
   * so we can directly cast the list data to guint32, no matter what the
   * actual structure is
   */
  for (; list; list = g_list_next (list)) {
    if (id == *((guint32 *) list->data)) {
      *link = list;
      return list->data;
    }
  }
  return NULL;
}

static struct object *
node_new (struct object *dev, uint32_t id,
    const struct spa_device_object_info *info)
{
  WpMonitor *self = dev->self;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpProxy) proxy = NULL;
  struct object *node = NULL;
  const gchar *pw_factory_name = "spa-node-factory";

  g_return_val_if_fail (g_strcmp0 (info->type, SPA_TYPE_INTERFACE_Node) == 0, NULL);

  g_debug ("WpMonitor:%p:%s new node %u", self, self->factory_name, id);

  /* use the adapter instead of spa-node-factory if requested */
  if (self->flags & WP_MONITOR_FLAG_USE_ADAPTER)
    pw_factory_name = "adapter";

  core = g_weak_ref_get (&self->core);
  props = wp_properties_new_copy_dict (info->props);

  /* pass down the id to the setup function */
  wp_properties_setf (props, WP_MONITOR_KEY_OBJECT_ID, "%u", id);

  /* the SPA factory name must be set as a property
     for the spa-node-factory / adapter */
  wp_properties_set (props, PW_KEY_FACTORY_NAME, info->factory_name);

  /* the rest is up to the user */
  g_signal_emit (self, signals[SIG_SETUP_NODE_PROPS], 0, dev->properties,
      props);

  /* and delete the id - it should not appear on the proxy */
  wp_properties_set (props, WP_MONITOR_KEY_OBJECT_ID, NULL);

  /* create the node locally or remotely */
  proxy = (self->flags & WP_MONITOR_FLAG_LOCAL_NODES) ?
      wp_core_create_local_object (core, pw_factory_name,
          PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, props) :
      wp_core_create_remote_object (core, pw_factory_name,
          PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, props);
  if (!proxy) {
    g_warning ("WpMonitor:%p: failed to create node: %s", self,
        g_strerror (errno));
    return NULL;
  }

  node = g_slice_new0 (struct object);
  node->self = self;
  node->id = id;
  node->type = WP_TYPE_PROXY_NODE;
  node->proxy = g_steal_pointer (&proxy);

  return node;
}

static void
set_profile(struct spa_device * dev, int index)
{
  char buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  spa_device_set_param (dev,
      SPA_PARAM_Profile, 0,
      spa_pod_builder_add_object(&b,
          SPA_TYPE_OBJECT_ParamProfile, 0,
          SPA_PARAM_PROFILE_index, SPA_POD_Int(index)));
}

static struct object *
device_new (WpMonitor *self, uint32_t id, const gchar *factory_name,
    WpProperties *properties, GError **error)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (WpCore) core = NULL;
  g_autoptr (WpProperties) props = NULL;
  g_autoptr (WpSpaObject) spa_dev = NULL;
  g_autoptr (WpProxy) proxy = NULL;
  struct object *dev = NULL;
  gint ret = 0;

  g_debug ("WpMonitor:%p:%s new device %d", self, self->factory_name, (gint) id);

  core = g_weak_ref_get (&self->core);
  props = properties ? wp_properties_copy (properties) : wp_properties_new_empty ();

  /* pass down the id to the setup function */
  wp_properties_setf (props, WP_MONITOR_KEY_OBJECT_ID, "%d", (gint) id);

  /* let the handler setup the properties accordingly */
  g_signal_emit (self, signals[SIG_SETUP_DEVICE_PROPS], 0, props);

  /* and delete the id - it should not appear on the proxy */
  wp_properties_set (props, WP_MONITOR_KEY_OBJECT_ID, NULL);

  /* load the spa device */
  spa_dev = load_spa_object (core, factory_name, SPA_TYPE_INTERFACE_Device,
          props, &err);
  if (!spa_dev) {
    g_propagate_error (error, g_steal_pointer (&err));
    return NULL;
  }

  /* check for id != -1 to avoid exporting the "monitor" device itself;
     exporting it is buggy, but we should revise this in the future; FIXME */
  if (id != -1 && !(proxy = wp_core_export_object (core,
          SPA_TYPE_INTERFACE_Device, spa_dev->interface, props))) {
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to export device: %s", g_strerror (errno));
    return NULL;
  }

  /* Create the device */
  dev = g_slice_new0 (struct object);
  dev->self = self;
  dev->id = id;
  dev->type = WP_TYPE_PROXY_DEVICE;
  dev->spa_obj = g_steal_pointer (&spa_dev);
  dev->properties = g_steal_pointer (&props);
  dev->proxy = g_steal_pointer (&proxy);

  /* Add device listener for events */
  ret = spa_device_add_listener (dev->spa_obj->interface,
      &dev->spa_obj->listener, &device_events, dev);
  if (ret < 0) {
    object_free (dev);
    g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
        "failed to initialize device: %s", spa_strerror (ret));
    return NULL;
  }

  /* HACK this is very specific to the current alsa pcm profiles */
  if (self->flags & WP_MONITOR_FLAG_ACTIVATE_DEVICES)
    set_profile (dev->spa_obj->interface, 1);

  return dev;
}

static void
object_free (struct object *obj)
{
  g_debug ("WpMonitor:%p:%s free %s %u", obj->self, obj->self->factory_name,
      g_type_name (obj->type), obj->id);

  g_list_free_full (obj->children, (GDestroyNotify) object_free);
  g_clear_object (&obj->proxy);

  g_clear_pointer (&obj->spa_obj, wp_spa_object_unref);

  g_clear_pointer (&obj->properties, wp_properties_unref);

  g_slice_free (struct object, obj);
}

/* WpMonitor */

static void
wp_monitor_init (WpMonitor * self)
{
  g_weak_ref_init (&self->core, NULL);
}

static void
wp_monitor_finalize (GObject * object)
{
  WpMonitor * self = WP_MONITOR (object);

  wp_monitor_stop (self);

  g_clear_pointer (&self->properties, wp_properties_unref);
  g_weak_ref_clear (&self->core);
  g_free (self->factory_name);

  G_OBJECT_CLASS (wp_monitor_parent_class)->finalize (object);
}

static void
wp_monitor_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpMonitor * self = WP_MONITOR (object);

  switch (property_id) {
  case PROP_CORE:
    g_weak_ref_set (&self->core, g_value_get_object (value));
    break;
  case PROP_FACTORY_NAME:
    self->factory_name = g_value_dup_string (value);
    break;
  case PROP_PROPERTIES:
    self->properties = g_value_dup_boxed (value);
    break;
  case PROP_FLAGS:
    self->flags = (WpMonitorFlags) g_value_get_flags (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_monitor_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  WpMonitor * self = WP_MONITOR (object);

  switch (property_id) {
  case PROP_CORE:
    g_value_take_object (value, g_weak_ref_get (&self->core));
    break;
  case PROP_FACTORY_NAME:
    g_value_set_string (value, self->factory_name);
    break;
  case PROP_PROPERTIES:
    g_value_set_boxed (value, self->properties);
    break;
  case PROP_FLAGS:
    g_value_set_flags (value, (guint) self->flags);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_monitor_class_init (WpMonitorClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = wp_monitor_finalize;
  object_class->set_property = wp_monitor_set_property;
  object_class->get_property = wp_monitor_get_property;

  /* Install the properties */
  g_object_class_install_property (object_class, PROP_CORE,
      g_param_spec_object ("core", "core", "The wireplumber core",
          WP_TYPE_CORE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_FACTORY_NAME,
      g_param_spec_string ("factory-name", "factory-name",
          "The factory name of the spa device", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "properties",
          "Properties for the spa device", WP_TYPE_PROPERTIES,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_FLAGS,
      g_param_spec_flags ("flags", "flags",
          "Additional feature flags", WP_TYPE_MONITOR_FLAGS, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /**
   * WpMonitor::setup-device-props:
   * @self: the #WpMonitor
   * @device_props: the properties of the device to be created
   *
   * This signal allows the handler to modify the properties of a device
   * object before it is created.
   */
  signals[SIG_SETUP_DEVICE_PROPS] = g_signal_new (
      "setup-device-props", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 1, WP_TYPE_PROPERTIES);

  /**
   * WpMonitor::setup-node-props:
   * @self: the #WpMonitor
   * @device_props: the properties of the parent device
   * @node_props: the properties of the node to be created
   *
   * This signal allows the handler to modify the properties of a node
   * object before it is created.
   */
  signals[SIG_SETUP_NODE_PROPS] = g_signal_new (
      "setup-node-props", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 2, WP_TYPE_PROPERTIES, WP_TYPE_PROPERTIES);
}

/**
 * wp_monitor_new:
 * @core: the wireplumber core
 * @factory_name: the factory name of the spa device
 * @props: properties to pass to the spa device
 * @flags: additional feature flags
 *
 * Returns: (transfer full): the newly created monitor
 */
WpMonitor *
wp_monitor_new (WpCore * core, const gchar * factory_name, WpProperties *props,
    WpMonitorFlags flags)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);
  g_return_val_if_fail (factory_name != NULL && *factory_name != '\0', NULL);

  return g_object_new (WP_TYPE_MONITOR,
      "core", core,
      "factory-name", factory_name,
      "properties", props,
      "flags", flags,
      NULL);
}

const gchar *
wp_monitor_get_factory_name (WpMonitor *self)
{
  g_return_val_if_fail (WP_IS_MONITOR (self), NULL);
  return self->factory_name;
}

gboolean
wp_monitor_start (WpMonitor *self, GError **error)
{
  g_autoptr (WpCore) core = NULL;
  g_autoptr (GError) err = NULL;

  g_return_val_if_fail (WP_IS_MONITOR (self), FALSE);

  core = g_weak_ref_get (&self->core);

  g_debug ("WpMonitor:%p:%s starting monitor, flags 0x%x", self,
      self->factory_name, self->flags);

  self->device = device_new (self, -1, self->factory_name, self->properties,
      &err);
  if (!self->device) {
    g_propagate_error (error, g_steal_pointer (&err));
    return FALSE;
  }

  return TRUE;
}

void
wp_monitor_stop (WpMonitor *self)
{
  g_return_if_fail (WP_IS_MONITOR (self));

  g_debug ("WpMonitor:%p:%s stopping monitor", self, self->factory_name);
  g_clear_pointer (&self->device, object_free);
}
