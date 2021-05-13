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

/*!
 * @fn WpIteratorFoldFunc:
 * @memberof WpIterator
 *
 * @param item: the item to fold
 * @param ret: the value collecting the result
 * @param data: data passed to @b wp_iterator_fold
 *
 * A function to be passed to `wp_iterator_fold()`.
 *
 * @returns TRUE if the fold should continue, FALSE if it should stop.
 */
typedef gboolean (*WpIteratorFoldFunc) (const GValue *item, GValue *ret,
    gpointer data);

/*!
 * @fn WpIteratorForeachFunc:
 * @memberof WpIterator
 *
 * @param item: the item
 * @param data: the data passed to @b wp_iterator_foreach
 *
 * A function that is called by `wp_iterator_foreach()`.
 */
typedef void (*WpIteratorForeachFunc) (const GValue *item, gpointer data);

/*!
 * memberof WpIterator
 *
 * @brief The [WpIterator](@ref iterator_section)
 * <a href="https://developer.gnome.org/gobject/stable/gobject-Type-Information.html#GType">
 * GType</a>
 *
 * @code
 * #define WP_TYPE_ITERATOR (wp_iterator_get_type ())
 * @endcode
 *
 */
#define WP_TYPE_ITERATOR (wp_iterator_get_type ())
WP_API
GType wp_iterator_get_type (void);

typedef struct _WpIterator WpIterator;
typedef struct _WpIteratorMethods WpIteratorMethods;

static const guint32 WP_ITERATOR_METHODS_VERSION = 0;

struct _WpIteratorMethods
{
  guint32 version;

  void (*reset) (WpIterator *self);
  gboolean (*next) (WpIterator *self, GValue *item);
  gboolean (*fold) (WpIterator *self, WpIteratorFoldFunc func,
      GValue *ret, gpointer data);
  gboolean (*foreach) (WpIterator *self, WpIteratorForeachFunc func,
      gpointer data);
  void (*finalize) (WpIterator *self);
};

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

WP_API
WpIterator * wp_iterator_new (const WpIteratorMethods * methods,
    size_t user_size);

/* private */

WP_API
gpointer wp_iterator_get_user_data (WpIterator * self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WpIterator, wp_iterator_unref)

G_END_DECLS

#endif
