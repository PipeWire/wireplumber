/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CONTEXT_H__
#define __WIREPLUMBER_CONTEXT_H__

#include <gio/gio.h>

#include "defs.h"
#include "object.h"
#include "properties.h"
#include "conf.h"
#include "spa-json.h"

G_BEGIN_DECLS

struct pw_context;
struct pw_loop;
struct pw_core;

typedef struct _WpConnection WpConnection;

/*!
 * \brief The WpContext GType
 * \ingroup wpcontext
 */
#define WP_TYPE_CONTEXT (wp_context_get_type ())
WP_API
G_DECLARE_DERIVABLE_TYPE (WpContext, wp_context, WP, CONTEXT, GObject)

struct _WpContextClass
{
  GObjectClass parent_class;

  struct pw_loop * (*get_pw_loop) (WpContext * self);

  WP_PADDING(7)
};

WP_API
WpCore * wp_context_get_core (WpContext * self);

WP_API
GMainContext * wp_context_get_g_main_context (WpContext * self);

WP_API
WpSpaJson * wp_context_get_spa_libs (WpContext * self);

WP_API
WpSpaJson * wp_context_get_modules (WpContext * self);

WP_API
WpProperties * wp_context_get_properties (WpContext * self);

WP_API
struct pw_context * wp_context_get_pw_context (WpContext * self);

WP_API
struct pw_core * wp_context_get_pw_core (WpContext * self);

WP_API
gchar * wp_context_get_vm_type (WpContext * self);

WP_API
void wp_context_idle_add (WpContext * self, GSource **source,
    GSourceFunc function, gpointer data, GDestroyNotify destroy);

WP_API
void wp_context_idle_add_closure (WpContext * self, GSource **source,
    GClosure * closure);

WP_API
void wp_context_timeout_add (WpContext * self, GSource **source,
    guint timeout_ms, GSourceFunc function, gpointer data,
    GDestroyNotify destroy);

WP_API
void wp_context_timeout_add_closure (WpContext * self, GSource **source,
    guint timeout_ms, GClosure * closure);


/*!
 * \brief The WpMainContext GType
 * \ingroup wpcontext
 */
#define WP_TYPE_MAIN_CONTEXT (wp_main_context_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpMainContext, wp_main_context, WP, MAIN_CONTEXT, WpContext)

WP_API
WpConnection * wp_main_context_get_connection (WpMainContext * self);


/*!
 * \brief The WpClientContext GType
 * \ingroup wpcontext
 */
#define WP_TYPE_CLIENT_CONTEXT (wp_client_context_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpClientContext, wp_client_context, WP, CLIENT_CONTEXT,
    WpContext)

WP_API
WpClientContext * wp_client_context_find (WpCore * core);

G_END_DECLS

#endif
