/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "loop-source.h"

#define WP_LOOP_SOURCE(x) ((WpLoopSource *) x)

typedef struct _WpLoopSource WpLoopSource;

struct _WpLoopSource
{
  GSource parent;
  struct pw_loop *loop;
};

static gboolean
wp_loop_source_dispatch (GSource * s, GSourceFunc callback, gpointer user_data)
{
  int result;

  pw_loop_enter (WP_LOOP_SOURCE(s)->loop);
  result = pw_loop_iterate (WP_LOOP_SOURCE(s)->loop, 0);
  pw_loop_leave (WP_LOOP_SOURCE(s)->loop);

  if (G_UNLIKELY (result < 0))
    g_warning ("pw_loop_iterate failed: %s", spa_strerror (result));

  return G_SOURCE_CONTINUE;
}

static void
wp_loop_source_finalize (GSource * s)
{
  pw_loop_destroy (WP_LOOP_SOURCE(s)->loop);
}

static GSourceFuncs source_funcs = {
  NULL,
  NULL,
  wp_loop_source_dispatch,
  wp_loop_source_finalize
};

GSource *
wp_loop_source_new (void)
{
  GSource *s = g_source_new (&source_funcs, sizeof (WpLoopSource));
  WP_LOOP_SOURCE(s)->loop = pw_loop_new (NULL);

  g_source_add_unix_fd (s,
      pw_loop_get_fd (WP_LOOP_SOURCE(s)->loop),
      G_IO_IN | G_IO_ERR | G_IO_HUP);

  return (GSource *) s;
}

struct pw_loop *
wp_loop_source_get_loop (GSource *s)
{
  return WP_LOOP_SOURCE(s)->loop;
}
