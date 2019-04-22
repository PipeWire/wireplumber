/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "session-registry-impl.h"
#include "plugin-registry-impl.h"
#include "utils.h"

#include <wp/session.h>
#include <wp/plugin.h>

typedef struct
{
  guint32 id;
  gchar *media_class;
  WpSession *session;
} SessionData;

struct _WpSessionRegistryImpl
{
  WpInterfaceImpl parent;

  guint32 next_id;
  GArray *sessions;
};

static void wp_session_registry_impl_iface_init (WpSessionRegistryInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpSessionRegistryImpl, wp_session_registry_impl, WP_TYPE_INTERFACE_IMPL,
    G_IMPLEMENT_INTERFACE (WP_TYPE_SESSION_REGISTRY, wp_session_registry_impl_iface_init);)

static void
wp_session_registry_impl_init (WpSessionRegistryImpl * self)
{
  self->sessions = g_array_new (FALSE, FALSE, sizeof (SessionData));
}

static void
wp_session_registry_impl_finalize (GObject * obj)
{
  WpSessionRegistryImpl * self = WP_SESSION_REGISTRY_IMPL (obj);

  g_array_unref (self->sessions);

  G_OBJECT_CLASS (wp_session_registry_impl_parent_class)->finalize (obj);
}

static void
wp_session_registry_impl_class_init (WpSessionRegistryImplClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  object_class->finalize = wp_session_registry_impl_finalize;
}

static gboolean
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
   * OK: media_class: Audio/Source/Virtual/
   *     lookup: Audio/Source
   *
   * Not OK: media_class: Audio/Source/Virtual/
   *         lookup: Audio/Sou
   *
   * if *c1 is not /, also check the previous char, because the lookup
   * may actually end in a slash.
   */
  if (*c1 != '/' && *(c1 - 1) != '/')
    return FALSE;

  return TRUE;
}

static gchar *
sanitize_media_class (const gchar *media_class)
{
  gsize len = strlen (media_class);
  if (media_class[len-1] != '/')
    return g_strdup_printf ("%s/", media_class);
  else
    return g_strdup (media_class);
}

static guint32
register_session (WpSessionRegistry * sr,
    WpSession * session,
    GError ** error)
{
  WpSessionRegistryImpl * self = WP_SESSION_REGISTRY_IMPL (sr);
  g_autoptr (WpPluginRegistry) plugin_registry = NULL;
  g_autoptr (WpPipewireProperties) pw_props = NULL;
  const gchar *media_class = NULL;
  SessionData data;

  plugin_registry = wp_interface_impl_get_sibling (WP_INTERFACE_IMPL (self),
      WP_TYPE_PLUGIN_REGISTRY);
  wp_plugin_registry_impl_invoke (plugin_registry,
      wp_plugin_provide_interfaces, WP_OBJECT (session));

  pw_props = wp_object_get_interface (WP_OBJECT (session),
      WP_TYPE_PIPEWIRE_PROPERTIES);
  if (!pw_props) {
    g_set_error (error, WP_DOMAIN_CORE, WP_CODE_INVALID_ARGUMENT,
        "session object does not implement WpPipewirePropertiesInterface");
    return -1;
  }

  media_class = wp_pipewire_properties_get (pw_props,
        WP_SESSION_PW_PROP_MEDIA_CLASS);
  if (!media_class) {
     g_set_error (error, WP_DOMAIN_CORE, WP_CODE_INVALID_ARGUMENT,
        "session media_class is NULL");
    return -1;
  }

  data.id = self->next_id++;
  data.media_class = sanitize_media_class (media_class);
  data.session = g_object_ref (session);
  g_array_append_val (self->sessions, data);

  return data.id;
}

static gboolean
unregister_session (WpSessionRegistry * sr, guint32 session_id)
{
  WpSessionRegistryImpl * self = WP_SESSION_REGISTRY_IMPL (sr);
  guint i;

  for (i = 0; i < self->sessions->len; i++) {
    SessionData *d = &g_array_index (self->sessions, SessionData, i);
    if (session_id == d->id) {
      g_free (d->media_class);
      g_object_unref (d->session);
      g_array_remove_index_fast (self->sessions, i);
      return TRUE;
    }
  }

  return FALSE;
}

static WpSession *
get_session (WpSessionRegistry * sr, guint32 session_id)
{
  WpSessionRegistryImpl * self = WP_SESSION_REGISTRY_IMPL (sr);
  guint i;

  for (i = 0; i < self->sessions->len; i++) {
    SessionData *d = &g_array_index (self->sessions, SessionData, i);
    if (session_id == d->id)
      return g_object_ref (d->session);
  }

  return NULL;
}

static GArray *
list_sessions (WpSessionRegistry * sr, const gchar * media_class)
{
  WpSessionRegistryImpl * self = WP_SESSION_REGISTRY_IMPL (sr);
  guint i;
  GArray *ret;

  ret = g_array_new (FALSE, FALSE, sizeof (guint32));

  for (i = 0; i < self->sessions->len; i++) {
    SessionData *d = &g_array_index (self->sessions, SessionData, i);
    if (media_class_matches (d->media_class, media_class))
      g_array_append_val (ret, d->id);
  }

  return ret;
}

static void
wp_session_registry_impl_iface_init (WpSessionRegistryInterface * iface)
{
  iface->register_session = register_session;
  iface->unregister_session = unregister_session;
  iface->get_session = get_session;
  iface->list_sessions = list_sessions;
}

WpSessionRegistryImpl *
wp_session_registry_impl_new (void)
{
  return g_object_new (wp_session_registry_impl_get_type (), NULL);
}
