/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * SECTION: session-bin
 * @title: Session Item Bin
 */

#define G_LOG_DOMAIN "wp-sb"

#include "session-bin.h"

typedef struct _WpSessionBinPrivate WpSessionBinPrivate;
struct _WpSessionBinPrivate
{
  GPtrArray *items;
};

/**
 * WpSessionBin:
 */
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (WpSessionBin, wp_session_bin, WP_TYPE_SESSION_ITEM)

static void
si_session_bin_reset (WpSessionItem * item)
{
  WpSessionBin * self = WP_SESSION_BIN (item);
  WpSessionBinPrivate *priv = wp_session_bin_get_instance_private (self);

  WP_SESSION_ITEM_CLASS (wp_session_bin_parent_class)->reset (item);

  g_ptr_array_set_size (priv->items, 0);
}

static void
wp_session_bin_finalize (GObject * object)
{
  WpSessionBin * self = WP_SESSION_BIN (object);
  WpSessionBinPrivate *priv = wp_session_bin_get_instance_private (self);

  g_clear_pointer (&priv->items, g_ptr_array_unref);

  G_OBJECT_CLASS (wp_session_bin_parent_class)->finalize (object);
}

static void
wp_session_bin_init (WpSessionBin * self)
{
  WpSessionBinPrivate *priv = wp_session_bin_get_instance_private (self);
  priv->items = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static void
wp_session_bin_class_init (WpSessionBinClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpSessionItemClass *si_class = (WpSessionItemClass *) klass;

  object_class->finalize = wp_session_bin_finalize;

  si_class->reset = si_session_bin_reset;
}

/**
 * wp_session_bin_new:
 * Creates a new session bin.
 *
 * Returns: TRUE if the item was added into the session bin, FALSE otherwise
 */
WpSessionBin *
wp_session_bin_new (void)
{
  return g_object_new (WP_TYPE_SESSION_BIN, NULL);
}

/**
 * wp_session_bin_add:
 * @self: the session bin
 * @item (transfer full): the session item to be added
 *
 * Adds a session item into a session bin.
 *
 * Returns: TRUE if the item was added into the session bin, FALSE otherwise
 */
gboolean
wp_session_bin_add (WpSessionBin *self, WpSessionItem *item)
{
  WpSessionBinPrivate *priv = wp_session_bin_get_instance_private (self);

  guint index;
  if (g_ptr_array_find (priv->items, item, &index))
    return FALSE;

  g_ptr_array_add (priv->items, item);
  wp_session_item_set_parent (item, WP_SESSION_ITEM (self));
  return TRUE;
}

/**
 * wp_session_bin_remove:
 * @self: the session bin
 * @item (transfer none): the session item to be removed
 *
 * Removes a session item from a session bin.
 *
 * Returns: TRUE if the item was removed from the session bin, FALSE otherwise
 */
gboolean
wp_session_bin_remove (WpSessionBin *self, WpSessionItem *item)
{
  WpSessionBinPrivate *priv = wp_session_bin_get_instance_private (self);
  wp_session_item_set_parent (item, NULL);
  return g_ptr_array_remove_fast (priv->items, item);
}

/**
 * wp_session_bin_get_n_children:
 * @self: the session bin
 *
 * Returns: the number of child items in the bin
 */
guint
wp_session_bin_get_n_children (WpSessionBin *self)
{
  WpSessionBinPrivate *priv = wp_session_bin_get_instance_private (self);
  return priv->items->len;
}

struct _WpSessionBinIterator
{
  WpSessionBin *bin;
  guint index;
};
typedef struct _WpSessionBinIterator WpSessionBinIterator;

static void
wp_session_bin_iterator_reset (WpIterator *iterator)
{
  WpSessionBinIterator *self = wp_iterator_get_user_data (iterator);
  self->index = 0;
}

static gboolean
wp_session_bin_iterator_next (WpIterator *iterator, GValue *item)
{
  WpSessionBinIterator *self = wp_iterator_get_user_data (iterator);
  WpSessionBinPrivate *priv = wp_session_bin_get_instance_private (self->bin);

  g_return_val_if_fail (item, FALSE);

  if (self->index < priv->items->len) {
    g_value_init_from_instance (item,
        g_ptr_array_index (priv->items, self->index++));
    return TRUE;
  }

  return FALSE;
}

static void
wp_session_bin_iterator_finalize (WpIterator *iterator)
{
  WpSessionBinIterator *self = wp_iterator_get_user_data (iterator);
  g_clear_object (&self->bin);
}

/**
 * wp_session_bin_new_iterator:
 * @self: the session bin
 * @item (transfer none): the session item to be removed
 *
 * Gets an iterator to iterate throught all session items.
 *
 * Returns (transfer full): The session bin iterator.
 */
WpIterator *
wp_session_bin_new_iterator (WpSessionBin *self)
{
  static const WpIteratorMethods methods = {
    .version = WP_ITERATOR_METHODS_VERSION,
    .reset = wp_session_bin_iterator_reset,
    .next = wp_session_bin_iterator_next,
    .fold = NULL,
    .foreach = NULL,
    .finalize = wp_session_bin_iterator_finalize
  };
  WpIterator *ret = wp_iterator_new (&methods, sizeof (WpSessionBinIterator));
  WpSessionBinIterator *it = wp_iterator_get_user_data (ret);

  it->bin = g_object_ref (self);

  return ret;
}
