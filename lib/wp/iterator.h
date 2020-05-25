/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_ITERATOR_H__
#define __WIREPLUMBER_ITERATOR_H__

#include <gio/gio.h>
#include "defs.h"

G_BEGIN_DECLS

/**
 * WpIteratorFoldFunc:
 * @item: the item to fold
 * @ret: the value collecting the result
 * @data: data passed to #wp_iterator_fold
 *
 * A function to be passed to #wp_iterator_fold.
 *
 * Returns: TRUE if the fold should continue, FALSE if it should stop.
 */
typedef gboolean (*WpIteratorFoldFunc) (const GValue *item, GValue *ret,
    gpointer data);

/**
 * WpIteratorForeachFunc:
 * @item: the item
 * @data: the data passed to #wp_iterator_foreach
 *
 * A function that is called by #wp_iterator_foreach for every element.
 */
typedef void (*WpIteratorForeachFunc) (const GValue *item, gpointer data);

/**
 * WP_TYPE_ITERATOR:
 *
 * The #WpIterator #GType
 */
#define WP_TYPE_ITERATOR (wp_iterator_get_type ())
WP_API
GType wp_iterator_get_type (void);

typedef struct _WpIterator WpIterator;

/* ref count */

WP_API
WpIterator *wp_iterator_ref (WpIterator *self);

WP_API
void wp_iterator_unref (WpIterator *self);

/* iteration api */

WP_API
void wp_iterator_reset (WpIterator *self);

WP_API
gboolean wp_iterator_next (WpIterator *self, GValue *item);

WP_API
gboolean wp_iterator_fold (WpIterator *self, WpIteratorFoldFunc func,
    GValue *ret, gpointer data);

WP_API
gboolean wp_iterator_foreach (WpIterator *self, WpIteratorForeachFunc func,
    gpointer data);

/* constructors */

WP_API
WpIterator * wp_iterator_new_ptr_array (GPtrArray * items, GType item_type);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpIterator, wp_iterator_unref)

G_END_DECLS

#endif
