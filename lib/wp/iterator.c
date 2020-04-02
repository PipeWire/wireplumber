/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "private.h"
#include "iterator.h"

struct _WpIterator
{
  const WpIteratorMethods *methods;
  gpointer user_data;
};

G_DEFINE_BOXED_TYPE (WpIterator, wp_iterator, wp_iterator_ref, wp_iterator_unref)

static gboolean
wp_iterator_default_fold (WpIterator *self, WpIteratorFoldFunc func,
    GValue *item, gpointer data)
{
  GValue next = G_VALUE_INIT;

  wp_iterator_reset (self);

  while (wp_iterator_next (self, &next)) {
    const gboolean res = func (&next, item, data);
    g_value_unset (&next);
    if (!res)
      return FALSE;
  }

  return TRUE;
}

struct foreach_fold_data {
  WpIteratorForeachFunc func;
  gpointer data;
};

static gboolean
foreach_fold_func (const GValue *item, GValue *ret, gpointer data)
{
  struct foreach_fold_data *d = data;
  d->func (item, d->data);
  return TRUE;
}

static gboolean
wp_iterator_default_foreach (WpIterator *self, WpIteratorForeachFunc func,
   gpointer data)
{
  struct foreach_fold_data d = {func, data};
  return wp_iterator_fold (self, foreach_fold_func, NULL, &d);
}

WpIterator *
wp_iterator_new (const WpIteratorMethods *methods, size_t user_size)
{
  WpIterator *self = NULL;

  g_return_val_if_fail (methods, NULL);

  self = g_rc_box_alloc0 (sizeof (WpIterator) + user_size);
  self->methods = methods;
  if (user_size > 0)
    self->user_data = SPA_MEMBER (self, sizeof (WpIterator), void);

  return self;
}

gpointer
wp_iterator_get_user_data (WpIterator *self)
{
  return self->user_data;
}

/**
 * wp_iterator_ref:
 * @self: an iterator object
 *
 * Returns: (transfer full): @self with an additional reference count on it
 */
WpIterator *
wp_iterator_ref (WpIterator *self)
{
  return (WpIterator *) g_rc_box_acquire ((gpointer) self);
}

static void
wp_iterator_free (WpIterator *self)
{
  if (self->methods->finalize)
    self->methods->finalize (self);
}

/**
 * wp_iterator_unref:
 * @self: (transfer full): an iterator object
 *
 * Decreases the reference count on @self and frees it when the ref count
 * reaches zero.
 */
void
wp_iterator_unref (WpIterator *self)
{
  g_rc_box_release_full (self, (GDestroyNotify) wp_iterator_free);
}

/**
 * wp_iterator_reset:
 * @self: the iterator
 *
 * Resets the iterator so we can iterate again from the beginning.
 */
void
wp_iterator_reset (WpIterator *self)
{
  g_return_if_fail (self);
  g_return_if_fail (self->methods->reset);

  self->methods->reset (self);
}

/**
 * wp_iterator_next:
 * @self: the iterator
 * @item: (out): the next item of the iterator
 *
 * Gets the next item of the iterator.
 *
 * Returns: TRUE if next iterator was obtained, FALSE when the iterator has no
 * more items to iterate through.
 */
gboolean
wp_iterator_next (WpIterator *self, GValue *item)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (self->methods->next, FALSE);

  return self->methods->next (self, item);
}

/**
 * wp_iterator_fold:
 * @self: the iterator
 * @func: (scope call): the fold function
 * @ret: the accumulator data
 * @data: the user data
 *
 * Iterates over all items of the iterator calling a function.
 *
 * Returns: TRUE if all the items were processed, FALSE otherwise.
 */
gboolean
wp_iterator_fold (WpIterator *self, WpIteratorFoldFunc func, GValue *ret,
    gpointer data)
{
  g_return_val_if_fail (self, FALSE);

  if (self->methods->fold)
    return self->methods->fold (self, func, ret, data);

  return wp_iterator_default_fold (self, func, ret, data);
}

/**
 * wp_iterator_foreach:
 * @self: the iterator
 * @func: (scope call): the foreach function
 * @data: the user data
 *
 * Fold a function over the items of the iterator.
 *
 * Returns: TRUE if all the items were processed, FALSE otherwise.
 */
gboolean
wp_iterator_foreach (WpIterator *self, WpIteratorForeachFunc func,
   gpointer data)
{
  g_return_val_if_fail (self, FALSE);

  if (self->methods->foreach)
    return self->methods->foreach (self, func, data);

  return wp_iterator_default_foreach (self, func, data);
}
