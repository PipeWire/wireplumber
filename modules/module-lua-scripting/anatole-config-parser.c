/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "anatole-config-parser.h"

struct _WpAnatoleConfigParser
{
  GObject parent;
  AnatoleEngine *engine;
};

enum {
  SIGNAL_LOAD_FUNCTIONS,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

static void wp_anatole_config_parser_iface_init (WpConfigParserInterface * iface);

G_DEFINE_TYPE_WITH_CODE (WpAnatoleConfigParser, wp_anatole_config_parser,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (WP_TYPE_CONFIG_PARSER,
                           wp_anatole_config_parser_iface_init))

static void
wp_anatole_config_parser_init (WpAnatoleConfigParser * self)
{
}

static void
wp_anatole_config_parser_finalize (GObject * object)
{
  WpAnatoleConfigParser * self = WP_ANATOLE_CONFIG_PARSER (object);

  g_clear_object (&self->engine);

  G_OBJECT_CLASS (wp_anatole_config_parser_parent_class)->finalize (object);
}

static void
wp_anatole_config_parser_class_init (WpAnatoleConfigParserClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;

  object_class->finalize = wp_anatole_config_parser_finalize;

  signals[SIGNAL_LOAD_FUNCTIONS] = g_signal_new ("load-functions",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, ANATOLE_TYPE_ENGINE);
}

static gboolean
wp_anatole_config_parser_add_file (WpConfigParser * parser, const gchar * file)
{
  WpAnatoleConfigParser * self = WP_ANATOLE_CONFIG_PARSER (parser);
  g_autoptr (GError) error = NULL;

  if (!anatole_engine_load_script_from_path (self->engine, file, &error)) {
    wp_warning_object (self, "failed to load '%s': %s", file, error->message);
    return FALSE;
  }
  return TRUE;
}

static void
wp_anatole_config_parser_reset (WpConfigParser * parser)
{
  WpAnatoleConfigParser * self = WP_ANATOLE_CONFIG_PARSER (parser);

  g_clear_object (&self->engine);
  self->engine = anatole_engine_new ("wp");
  g_signal_emit (self, signals[SIGNAL_LOAD_FUNCTIONS], 0, self->engine);
}

static void
wp_anatole_config_parser_iface_init (WpConfigParserInterface * iface)
{
  iface->add_file = wp_anatole_config_parser_add_file;
  iface->reset = wp_anatole_config_parser_reset;
}

AnatoleEngine *
wp_anatole_config_parser_get_engine (WpAnatoleConfigParser * self)
{
  g_return_val_if_fail (WP_IS_ANATOLE_CONFIG_PARSER (self), NULL);
  return self->engine;
}
