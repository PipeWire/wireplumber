/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_PRIVATE_CONTEXT_H__
#define __WIREPLUMBER_PRIVATE_CONTEXT_H__

#include "../context.h"

G_BEGIN_DECLS

/* Main Context */

WpMainContext * wp_main_context_new (WpCore * core,
    GMainContext * g_main_context, WpProperties * properties,
    WpSpaJson * spa_libs, WpSpaJson * modules);


/* Client Context */

typedef void (*WpClientContextFunc) (gpointer data);

WpClientContext * wp_client_context_new (WpCore * core,
    GMainContext * g_main_context, WpProperties * properties,
    WpSpaJson * spa_libs, WpSpaJson * modules);

gboolean wp_client_context_start (WpClientContext * self, GError ** error);

void wp_client_context_stop (WpClientContext * self);

void wp_client_context_lock (WpClientContext * self);

void wp_client_context_unlock (WpClientContext * self);

gboolean wp_client_context_in_thread (WpClientContext * self);

void wp_client_context_run_in_thread (WpClientContext * self,
    WpClientContextFunc callback, gpointer data);

void wp_client_context_queue_in_main (WpClientContext * self,
    WpClientContextFunc callback, gpointer data, GDestroyNotify destroy);

G_END_DECLS

#endif
