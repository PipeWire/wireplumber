/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author Pauli Virtanen <pav@iki.fi>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

#include <libintl.h>

#define NAME "i18n"

struct _WpI18n
{
  WpPlugin parent;
};

enum {
  ACTION_GETTEXT,
  ACTION_NGETTEXT,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

G_DECLARE_FINAL_TYPE (WpI18n, wp_i18n,
                      WP, I18N, WpPlugin)
G_DEFINE_TYPE (WpI18n, wp_i18n, WP_TYPE_PLUGIN)

static void
wp_i18n_init (WpI18n * self)
{
}

static gchar *
wp_i18n_gettext (WpI18n * self, const gchar * msgid)
{
  return g_strdup (dgettext (GETTEXT_PACKAGE, msgid));
}

static gchar *
wp_i18n_ngettext (WpI18n * self, const gchar * msgid, const gchar *msgid_plural, gulong n)
{
  return g_strdup (dngettext (GETTEXT_PACKAGE, msgid, msgid_plural, n));
}

static void
wp_i18n_enable (WpPlugin * plugin, WpTransition * transition)
{
  WpI18n * self = WP_I18N (plugin);

  wp_object_update_features (WP_OBJECT (self), WP_PLUGIN_FEATURE_ENABLED, 0);
}

static void
wp_i18n_disable (WpPlugin * plugin)
{
}

static void
wp_i18n_class_init (WpI18nClass * klass)
{
  WpPluginClass *plugin_class = (WpPluginClass *) klass;

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  plugin_class->enable = wp_i18n_enable;
  plugin_class->disable = wp_i18n_disable;

  signals[ACTION_GETTEXT] = g_signal_new_class_handler (
      "gettext", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_i18n_gettext,
      NULL, NULL, NULL, G_TYPE_STRING, 1,
      G_TYPE_STRING);

  signals[ACTION_NGETTEXT] = g_signal_new_class_handler (
      "ngettext", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      (GCallback) wp_i18n_ngettext,
      NULL, NULL, NULL, G_TYPE_STRING, 3,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_ULONG);
}

WP_PLUGIN_EXPORT gboolean
wireplumber__module_init (WpCore * core, GVariant * args, GError ** error)
{
  wp_plugin_register (g_object_new (wp_i18n_get_type (),
          "name", NAME,
          "core", core,
          NULL));
  return TRUE;
}
