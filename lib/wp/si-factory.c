/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file si-factory.c
 */

#define G_LOG_DOMAIN "wp-si-factory"

#include "si-factory.h"
#include "private/registry.h"

/*!
 * @memberof WpSiFactory
 *
 * @props @b name
 *
 * @code
 * "name" gchar *
 * @endcode
 *
 * Flags : Read / Write / Construct Only
 *
 */
enum {
  PROP_0,
  PROP_NAME,
};

typedef struct _WpSiFactoryPrivate WpSiFactoryPrivate;
struct _WpSiFactoryPrivate
{
  GQuark name_quark;
};

/*!
 * @struct WpSiFactory
 * @section si_factory_section Session Items Factory
 *
 * @brief A factory for session items.
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
 *
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

/*!
 * @memberof WpSiFactory
 * @param self: the factory
 *
 * @returns the factory name
 */

const gchar *
wp_si_factory_get_name (WpSiFactory * self)
{
  g_return_val_if_fail (WP_IS_SI_FACTORY (self), NULL);

  WpSiFactoryPrivate *priv = wp_si_factory_get_instance_private (self);
  return g_quark_to_string (priv->name_quark);
}

/*!
 * @memberof WpSiFactory
 * @param self: the factory
 * @param core: the core
 *
 * @brief Creates a new instance of the session item that is constructed
 * by this factory
 *
 * @returns (transfer full): a new session item instance
 */

WpSessionItem *
wp_si_factory_construct (WpSiFactory * self, WpCore * core)
{
  g_return_val_if_fail (WP_IS_SI_FACTORY (self), NULL);
  g_return_val_if_fail (WP_SI_FACTORY_GET_CLASS (self)->construct, NULL);

  return WP_SI_FACTORY_GET_CLASS (self)->construct (self, core);
}

/*!
 * @memberof WpSiFactory
 * @param core: the core
 * @param factory: (transfer full): the factory to register
 *
 * @brief Registers the @em factory on the @em core.
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

/*!
 * @memberof WpSiFactory
 * @param core: the core
 * @param factory_name: the lookup name
 *
 * @returns (transfer full) (nullable): the factory matching the lookup name
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

/*!
 * @memberof WpSiFactory
 * @param core: the [WpCore](@ref core_section)
 * @param factory_name: the name of the factory to be used for constructing the object
 *
 * @brief Finds the factory associated with the given @em name from the @em core and
 * uses it to construct a new [WpSessionItem](@ref session_item_section).
 *
 * @returns (transfer full) (nullable): the new session item
 */

WpSessionItem *
wp_session_item_make (WpCore * core, const gchar * factory_name)
{
  g_autoptr (WpSiFactory) f = wp_si_factory_find (core, factory_name);
  return f ? wp_si_factory_construct (f, core) : NULL;
}

struct _WpSimpleSiFactory
{
  WpSiFactory parent;
  GType si_type;
};

G_DECLARE_FINAL_TYPE (WpSimpleSiFactory, wp_simple_si_factory,
                      WP, SIMPLE_SI_FACTORY, WpSiFactory)
G_DEFINE_TYPE (WpSimpleSiFactory, wp_simple_si_factory, WP_TYPE_SI_FACTORY)

static void
wp_simple_si_factory_init (WpSimpleSiFactory * self)
{
}

static WpSessionItem *
wp_simple_si_factory_construct (WpSiFactory * self, WpCore *core)
{
  return g_object_new (WP_SIMPLE_SI_FACTORY (self)->si_type,
      "core", core,
      NULL);
}

static void
wp_simple_si_factory_class_init (WpSimpleSiFactoryClass * klass)
{
  WpSiFactoryClass * factory_class = (WpSiFactoryClass *) klass;

  factory_class->construct = wp_simple_si_factory_construct;
}

/*!
 * @memberof WpSiFactory
 * @param factory_name: the factory name; must be a static string!
 * @param si_type: the [WpSessionItem](@ref session_item_section) subclass type to instantiate for
 *    constructing items
 *
 * @returns (transfer full): the new factory
 */

WpSiFactory *
wp_si_factory_new_simple (const gchar * factory_name, GType si_type)
{
  g_return_val_if_fail (factory_name != NULL, NULL);
  g_return_val_if_fail (g_type_is_a (si_type, WP_TYPE_SESSION_ITEM), NULL);

  WpSimpleSiFactory *self = g_object_new (
      wp_simple_si_factory_get_type (), NULL);

  /* assign the quark directly to use g_quark_from_static_string */
  WpSiFactoryPrivate *priv =
      wp_si_factory_get_instance_private (WP_SI_FACTORY (self));
  priv->name_quark = g_quark_from_static_string (factory_name);

  self->si_type = si_type;

  return WP_SI_FACTORY (self);
}