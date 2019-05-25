/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "session-manager.h"
#include "endpoint.h"

struct _WpSessionManager
{
  GObject parent;
  GPtrArray *endpoints;
};

enum {
  SIGNAL_ENDPOINT_ADDED,
  SIGNAL_ENDPOINT_REMOVED,
  NUM_SIGNALS
};

static guint32 signals[NUM_SIGNALS];

G_DEFINE_TYPE (WpSessionManager, wp_session_manager, G_TYPE_OBJECT)
G_DEFINE_QUARK (WP_GLOBAL_SESSION_MANAGER, wp_global_session_manager)

static void
wp_session_manager_init (WpSessionManager * self)
{
  self->endpoints = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
wp_session_manager_finalize (GObject * obj)
{
  WpSessionManager * self = WP_SESSION_MANAGER (obj);

  g_ptr_array_unref (self->endpoints);

  G_OBJECT_CLASS (wp_session_manager_parent_class)->finalize (obj);
}

static void
wp_session_manager_class_init (WpSessionManagerClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->finalize = wp_session_manager_finalize;

  signals[SIGNAL_ENDPOINT_ADDED] = g_signal_new ("endpoint-added",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_ENDPOINT);

  signals[SIGNAL_ENDPOINT_REMOVED] = g_signal_new ("endpoint-removed",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, WP_TYPE_ENDPOINT);
}

WpSessionManager *
wp_session_manager_new (void)
{
  return g_object_new (WP_TYPE_SESSION_MANAGER, NULL);
}

void
wp_session_manager_add_endpoint (WpSessionManager * self, WpEndpoint * ep)
{
  g_ptr_array_add (self->endpoints, g_object_ref (ep));
  g_signal_emit (self, signals[SIGNAL_ENDPOINT_ADDED], 0, ep);
}

void
wp_session_manager_remove_endpoint (WpSessionManager * self, WpEndpoint * ep)
{
  g_signal_emit (self, signals[SIGNAL_ENDPOINT_REMOVED], 0, ep);
  g_ptr_array_remove_fast (self->endpoints, ep);
}

struct endpoints_foreach_data
{
  GPtrArray *result;
  const gchar *lookup;
};

static inline gboolean
media_class_matches (const gchar * media_class, const gchar * lookup)
{
  const gchar *c1 = media_class, *c2 = lookup;

  /* empty lookup matches all classes */
  if (!lookup)
    return TRUE;

  /* compare until we reach the end of the lookup string */
  for (; *c2 != '\0'; c1++, c2++) {
    if (*c1 != *c2)
      return FALSE;
  }

  /* the lookup may not end in a slash, however it must match up
   * to the end of a submedia_class. i.e.:
   * match: media_class: Audio/Source/Virtual
   *        lookup: Audio/Source
   *
   * NO match: media_class: Audio/Source/Virtual
   *           lookup: Audio/Sou
   *
   * if *c1 is not /, also check the previous char, because the lookup
   * may actually end in a slash:
   *
   * match: media_class: Audio/Source/Virtual
   *        lookup: Audio/Source/
   */
  if (!(*c1 == '/' || *c1 == '\0' || *(c1 - 1) == '/'))
    return FALSE;

  return TRUE;
}

static void
find_endpoints (WpEndpoint * endpoint, struct endpoints_foreach_data * data)
{
  if (media_class_matches (wp_endpoint_get_media_class (endpoint), data->lookup))
    g_ptr_array_add (data->result, g_object_ref (endpoint));
}

GPtrArray *
wp_session_manager_find_endpoints (WpSessionManager * self,
    const gchar * media_class_lookup)
{
  struct endpoints_foreach_data data;
  data.result = g_ptr_array_new_with_free_func (g_object_unref);
  data.lookup = media_class_lookup;
  g_ptr_array_foreach (self->endpoints, (GFunc) find_endpoints, &data);
  return data.result;
}
