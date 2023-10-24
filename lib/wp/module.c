/* WirePlumber
 *
 * Copyright © 2021 Asymptotic
 *    @author Arun Raghavan <arun@asymptotic.io>
 *
 * SPDX-License-Identifier: MIT
 */

#include "module.h"
#include "log.h"

#include <pipewire/impl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-module")

/*! \defgroup wpimplmodule WpImplModule */
/*!
 * \struct WpImplModule
 * \since 0.4.2
 *
 * Used to load PipeWire modules within the WirePlumber process. This is
 * slightly different from other objects in that the module is not exported to
 * PipeWire, but it may create an export objects itself.
 */

struct _WpImplModule
{
  GObject parent;

  GWeakRef core;
  gchar *name;
  gchar *args;
  WpProperties *props; /* only used during module load */

  struct pw_impl_module *pw_impl_module;
};

G_DEFINE_TYPE (WpImplModule, wp_impl_module, G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_CORE,
  PROP_NAME,
  PROP_ARGUMENTS,
  PROP_PROPERTIES,
  PROP_PW_IMPL_MODULE,
};

static void
wp_impl_module_init (WpImplModule * self)
{
  g_weak_ref_init (&self->core, NULL);
  self->name = NULL;
  self->args = NULL;
  self->props = NULL;
  self->pw_impl_module = NULL;
}

static void
wp_impl_module_constructed (GObject * object)
{
  WpImplModule *self = WP_IMPL_MODULE (object);
  WpCore *core = g_weak_ref_get (&self->core);
  struct pw_context *context = core ? wp_core_get_pw_context (core) : NULL;
  struct pw_properties *props = NULL;

  if (!core || !context) {
    g_warning ("Tried to load module on unconnected core");
    return;
  }

  if (!self->name) {
    g_warning ("Invalid name while loading warnings");
    return;
  }

  if (self->props)
    props = wp_properties_to_pw_properties (self->props);

  self->pw_impl_module =
    pw_context_load_module (context, self->name, self->args, props);

  if (self->pw_impl_module && self->props) {
    /* With the module loaded, properties are just passthrough now */
    wp_properties_unref (self->props);
    self->props = NULL;
  }

  G_OBJECT_CLASS (wp_impl_module_parent_class)->constructed (object);
}

static void
wp_impl_module_finalize (GObject * object)
{
  WpImplModule *self = WP_IMPL_MODULE (object);

  g_weak_ref_clear (&self->core);

  if (self->pw_impl_module)
    pw_impl_module_destroy (self->pw_impl_module);

  g_free (self->name);
  g_free (self->args);

  if (self->props)
    wp_properties_unref (self->props);
}

static void
wp_impl_module_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  WpImplModule *self = WP_IMPL_MODULE (object);

  switch (prop_id) {
    case PROP_CORE:
      g_value_set_pointer (value, g_weak_ref_get (&self->core));
      break;

    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;

    case PROP_ARGUMENTS:
      g_value_set_string (value, self->args);
      break;

    case PROP_PROPERTIES:
      if (self->pw_impl_module) {
        const struct pw_properties *props =
          pw_impl_module_get_properties (self->pw_impl_module);

        /* Should we just wrap instead of copying? */
        if (props)
          g_value_set_boxed (value, wp_properties_new_copy (props));
        else
          g_value_set_boxed (value, NULL);
      } else {
        g_value_set_boxed (value, self->props);
      }
      break;

    case PROP_PW_IMPL_MODULE:
      g_value_set_pointer (value, self->pw_impl_module);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
wp_impl_module_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  WpImplModule *self = WP_IMPL_MODULE (object);
  WpProperties *props;

  switch (prop_id) {
    case PROP_CORE:
      g_weak_ref_set (&self->core, g_value_get_pointer (value));
      break;

    case PROP_NAME:
      g_free (self->name);
      self->name = g_value_dup_string (value);
      break;

    case PROP_ARGUMENTS:
      g_free (self->args);
      self->args = g_value_dup_string (value);
      break;

    case PROP_PROPERTIES:
      props = g_value_get_boxed (value);

      if (props && self->pw_impl_module) {
        pw_impl_module_update_properties (self->pw_impl_module,
            wp_properties_peek_dict (props));
      } else {
        if (props)
          self->props = wp_properties_ref (props);
        else
          self->props = NULL;
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
wp_impl_module_class_init (WpImplModuleClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->constructed = wp_impl_module_constructed;
  gobject_class->finalize = wp_impl_module_finalize;
  gobject_class->get_property = wp_impl_module_get_property;
  gobject_class->set_property = wp_impl_module_set_property;

  g_object_class_install_property (gobject_class, PROP_CORE,
      g_param_spec_pointer ("core", "Core", "The WirePlumber core",
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NAME,
      g_param_spec_string ("name", "Name", "The name of the PipeWire module",
        NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ARGUMENTS,
      g_param_spec_string ("arguments", "Arguments",
        "The arguments to provide to the module while loading", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROPERTIES,
      g_param_spec_boxed ("properties", "Properties",
        "Properties of the module", WP_TYPE_PROPERTIES,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PW_IMPL_MODULE,
      g_param_spec_pointer ("pw-impl-module", "Underlying pw_impl_module",
        "Pointer to the underlying pw_impl_module structure for the module",
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

/*!
 * \brief Loads a PipeWire module into the WirePlumber process
 *
 * \ingroup wpimplmodule
 * \since 0.4.2
 * \param core (transfer none): The WirePlumber core
 * \param name (transfer none): the name of the module to load
 * \param arguments (nullable) (transfer none): arguments to be passed to the module
 * \param properties (nullable) (transfer none): additional properties to be
 *    provided to the module
 * \returns (nullable) (transfer full): the WpImplModule for the module that
 *    was loaded on success, %NULL on failure.
 */
WpImplModule *
wp_impl_module_load (WpCore * core, const gchar * name,
    const gchar * arguments, WpProperties * properties)
{
  WpImplModule *module = WP_IMPL_MODULE (
      g_object_new (WP_TYPE_IMPL_MODULE,
        "core", core,
        "name", name,
        "arguments", arguments,
        "properties", properties,
        NULL)
      );

  if (!module->pw_impl_module) {
    /* Module loading failed, free and return */
    g_object_unref (module);
    return NULL;
  }

  return module;
}

/*!
 * \brief Loads a PipeWire module with arguments from file into the WirePlumber process
 *
 * \ingroup wpimplmodule
 * \since 0.4.15
 * \param core (transfer none): The WirePlumber core
 * \param name (transfer none): the name of the module to load
 * \param filename (transfer none): filename to be used as arguments
 * \param properties (nullable) (transfer none): additional properties to be
 *    provided to the module
 * \returns (nullable) (transfer full): the WpImplModule for the module that
 *    was loaded on success, %NULL on failure.
 */
WpImplModule *
wp_impl_module_load_file (WpCore * core, const gchar * name,
    const gchar * filename, WpProperties * properties)
{
  char *config = "";
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    g_warning("Failed to open config file %s: %m", filename);
    return NULL;
  }

  struct stat stats;
  int err = fstat(fd, &stats);
  if (err < 0) {
    g_warning("Failed to stat config file %s: %m", filename);
    close(fd);
    return NULL;
  }

  config = mmap(NULL, stats.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (config == MAP_FAILED){
    g_warning("Failed to mmap config file %s: %m", filename);
    close(fd);
    return NULL;
  }
  close(fd);

  WpImplModule *module = WP_IMPL_MODULE (
      g_object_new (WP_TYPE_IMPL_MODULE,
        "core", core,
        "name", name,
        "arguments", config,
        "properties", properties,
        NULL)
      );

  munmap(config, stats.st_size);

  if (!module->pw_impl_module) {
    /* Module loading failed, free and return */
    g_object_unref (module);
    return NULL;
  }

  return module;
}
