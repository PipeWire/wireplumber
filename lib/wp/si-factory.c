/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: si-factory
 * @title: Session Items Factory
 */

#define G_LOG_DOMAIN "wp-si-factory"

#include "si-factory.h"
#include "private/registry.h"

enum {
  PROP_0,
  PROP_NAME,
};

typedef struct _WpSiFactoryPrivate WpSiFactoryPrivate;
struct _WpSiFactoryPrivate
{
  GQuark name_quark;
};

/**
 * WpSiFactory:
 *
 * A factory for session items.
 *
 * The most simple way to register a new item implementation would be:
 * |[
 * GVariantBuilder b = G_VARIANT_BUILDER_INIT ("a(ssymv)");
 * g_variant_builder_add (&b, ...);
 * wp_si_factory_register (core, wp_si_factory_new_simple (
 *    "foobar", FOO_TYPE_BAR, g_variant_builder_end (&b)));
 * ]|
 *
 * And the most simple way to construct an item from a registered factory:
 * |[
 * item = wp_session_item_make (core, "foobar");
 * ]|
 */
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpSiFactory, wp_si_factory, G_TYPE_OBJECT)

static void
wp_si_factory_init (WpSiFactory * self)
{
}

static void
wp_si_factory_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  WpSiFactory *self = WP_SI_FACTORY (object);
  WpSiFactoryPrivate *priv = wp_si_factory_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    priv->name_quark = g_quark_from_string (g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_si_factory_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  WpSiFactory *self = WP_SI_FACTORY (object);
  WpSiFactoryPrivate *priv = wp_si_factory_get_instance_private (self);

  switch (property_id) {
  case PROP_NAME:
    g_value_set_string (value, g_quark_to_string (priv->name_quark));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_si_factory_class_init (WpSiFactoryClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->get_property = wp_si_factory_get_property;
  object_class->set_property = wp_si_factory_set_property;

  g_object_class_install_property (object_class, PROP_NAME,
      g_param_spec_string ("name", "name", "The factory's name", "",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

/**
 * wp_si_factory_get_name:
 * @self: the factory
 *
 * Returns: the factory name
 */
const gchar *
wp_si_factory_get_name (WpSiFactory * self)
{
  g_return_val_if_fail (WP_IS_SI_FACTORY (self), NULL);

  WpSiFactoryPrivate *priv = wp_si_factory_get_instance_private (self);
  return g_quark_to_string (priv->name_quark);
}

/**
 * wp_si_factory_construct:
 * @self: the factory
 *
 * Creates a new instance of the session item that is constructed
 * by this factory
 *
 * Returns: (transfer full): a new session item instance
 */
WpSessionItem *
wp_si_factory_construct (WpSiFactory * self)
{
  g_return_val_if_fail (WP_IS_SI_FACTORY (self), NULL);
  g_return_val_if_fail (WP_SI_FACTORY_GET_CLASS (self)->construct, NULL);

  return WP_SI_FACTORY_GET_CLASS (self)->construct (self);
}

/**
 * wp_si_factory_get_config_spec:
 * @self: the factory
 *
 * Returns a description of all the configuration options that the constructed
 * items of this factory have. Configuration options are a way for items to
 * accept input from external sources that affects their behavior, or to
 * provide output for other items to consume as their configuration.
 *
 * The returned GVariant has the a(ssymv) type. This is an array of tuples,
 * where each tuple has the following values, in order:
 *  * s (string): the name of the option
 *  * s (string): a GVariant type string, describing the type of the data
 *  * y (byte): a combination of #WpSiConfigOptionFlags
 *  * mv (optional variant): optionally, an additional variant
 *    This is provided to allow extensions.
 *
 * Returns: (transfer full): the configuration description
 */
GVariant *
wp_si_factory_get_config_spec (WpSiFactory * self)
{
  g_return_val_if_fail (WP_IS_SI_FACTORY (self), NULL);
  g_return_val_if_fail (WP_SI_FACTORY_GET_CLASS (self)->get_config_spec, NULL);

  return WP_SI_FACTORY_GET_CLASS (self)->get_config_spec (self);
}

/**
 * wp_si_factory_register:
 * @core: the core
 * @factory: (transfer full): the factory to register
 *
 * Registers the @factory on the @core.
 */
void
wp_si_factory_register (WpCore * core, WpSiFactory * factory)
{
  g_return_if_fail (WP_IS_CORE (core));
  g_return_if_fail (WP_IS_SI_FACTORY (factory));

  wp_registry_register_object (wp_core_get_registry (core), factory);
}

static gboolean
find_factory_func (gpointer factory, gpointer name_quark)
{
  if (!WP_IS_SI_FACTORY (factory))
    return FALSE;

  WpSiFactoryPrivate *priv =
      wp_si_factory_get_instance_private ((WpSiFactory *) factory);
  return priv->name_quark == GPOINTER_TO_UINT (name_quark);
}

/**
 * wp_si_factory_find:
 * @core: the core
 * @factory_name: the lookup name
 *
 * Returns: (transfer full) (nullable): the factory matching the lookup name
 */
WpSiFactory *
wp_si_factory_find (WpCore * core, const gchar * factory_name)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  GQuark q = g_quark_try_string (factory_name);
  if (q == 0)
    return NULL;
  GObject *f = wp_registry_find_object (wp_core_get_registry (core),
      (GEqualFunc) find_factory_func, GUINT_TO_POINTER (q));
  return f ? WP_SI_FACTORY (f) : NULL;
}

/**
 * wp_session_item_make:
 * @core: the #WpCore
 * @factory_name: the name of the factory to be used for constructing the object
 *
 * Finds the factory associated with the given @name from the @core and
 * uses it to construct a new #WpSessionItem.
 *
 * Returns: (transfer full) (nullable): the new session item
 */
WpSessionItem *
wp_session_item_make (WpCore * core, const gchar * factory_name)
{
  g_autoptr (WpSiFactory) f = wp_si_factory_find (core, factory_name);
  return f ? wp_si_factory_construct (f) : NULL;
}

struct _WpSimpleSiFactory
{
  WpSiFactory parent;
  GType si_type;
  GVariant *config_spec;
};

G_DECLARE_FINAL_TYPE (WpSimpleSiFactory, wp_simple_si_factory,
                      WP, SIMPLE_SI_FACTORY, WpSiFactory)
G_DEFINE_TYPE (WpSimpleSiFactory, wp_simple_si_factory, WP_TYPE_SI_FACTORY)

static void
wp_simple_si_factory_init (WpSimpleSiFactory * self)
{
}

static void
wp_simple_si_factory_finalize (GObject * object)
{
  WpSimpleSiFactory * self = WP_SIMPLE_SI_FACTORY (object);

  g_clear_pointer (&self->config_spec, g_variant_unref);

  G_OBJECT_CLASS (wp_simple_si_factory_parent_class)->finalize (object);
}

static WpSessionItem *
wp_simple_si_factory_construct (WpSiFactory * self)
{
  return g_object_new (WP_SIMPLE_SI_FACTORY (self)->si_type, NULL);
}

static GVariant *
wp_simple_si_factory_get_config_spec (WpSiFactory * self)
{
  return g_variant_ref (WP_SIMPLE_SI_FACTORY (self)->config_spec);
}

static void
wp_simple_si_factory_class_init (WpSimpleSiFactoryClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  WpSiFactoryClass * factory_class = (WpSiFactoryClass *) klass;

  object_class->finalize = wp_simple_si_factory_finalize;

  factory_class->construct = wp_simple_si_factory_construct;
  factory_class->get_config_spec = wp_simple_si_factory_get_config_spec;
}

/**
 * wp_si_factory_new_simple:
 * @factory_name: the factory name; must be a static string!
 * @si_type: the #WpSessionItem subclass type to instantiate for
 *    constructing items
 * @config_spec: (transfer floating): the config spec that will be returned
 *    by wp_si_factory_get_config_spec()
 *
 * Returns: (transfer full): the new factory
 */
WpSiFactory *
wp_si_factory_new_simple (const gchar * factory_name,
    GType si_type, GVariant * config_spec)
{
  g_return_val_if_fail (factory_name != NULL, NULL);
  g_return_val_if_fail (g_type_is_a (si_type, WP_TYPE_SESSION_ITEM), NULL);
  g_return_val_if_fail (
      g_variant_is_of_type (config_spec, G_VARIANT_TYPE ("a(ssymv)")), NULL);

  WpSimpleSiFactory *self = g_object_new (
      wp_simple_si_factory_get_type (), NULL);

  /* assign the quark directly to use g_quark_from_static_string */
  WpSiFactoryPrivate *priv =
      wp_si_factory_get_instance_private (WP_SI_FACTORY (self));
  priv->name_quark = g_quark_from_static_string (factory_name);

  self->si_type = si_type;
  self->config_spec = g_variant_ref_sink (config_spec);

  return WP_SI_FACTORY (self);
}
