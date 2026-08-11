/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_CLIENT_CONTEXT_H__
#define __WIREPLUMBER_CLIENT_CONTEXT_H__

#include "../core.h"
#include "../proxy.h"
#include "../spa-json.h"

G_BEGIN_DECLS

struct pw_context;
struct pw_core;

/*
 * WpClientContext is a secondary pw_context that runs on its own
 * pw_thread_loop, with its own connection to PipeWire. It hosts every PipeWire
 * object that WirePlumber implements in-process: PipeWire modules loaded with
 * WpImplModule, nodes created with WpImplNode and spa_device handles wrapped by
 * WpSpaDevice.
 *
 * The purpose is isolation: WirePlumber's main GMainContext also dispatches the
 * event dispatcher, whose hooks are Lua and can run for an arbitrarily long
 * time. GSource priorities do not preempt, so anything sharing that loop stalls
 * for as long as a hook runs. Media objects cannot afford that.
 *
 * Threading contract
 * ------------------
 * - All GObjects (WpImplModule, WpImplNode, WpSpaDevice, ...) remain owned by
 *   the thread that runs the WpCore's GMainContext ("the main thread"). Nothing
 *   about the public API changes.
 * - Calls from the main thread into pw_*() / spa_*() functions that operate on
 *   objects owned by this context MUST be made between
 *   wp_client_context_lock() and wp_client_context_unlock(). While the lock is
 *   held the loop thread is not running, so there is no re-entrancy. Keep those
 *   sections to pw and spa calls only: never call back into WirePlumber and
 *   never emit a signal while holding the lock.
 * - Callbacks that arrive on the loop thread and need to emit signals or touch
 *   WirePlumber state MUST be handed to the main thread with
 *   wp_client_context_invoke_main(), which preserves FIFO order.
 */
#define WP_TYPE_CLIENT_CONTEXT (wp_client_context_get_type ())
G_DECLARE_FINAL_TYPE (WpClientContext, wp_client_context,
                      WP, CLIENT_CONTEXT, GObject)

/*
 * Creates the client context. \a args is the "arguments" object of the
 * client-context component and may be NULL; recognized keys are
 * "context.properties", "context.spa-libs" and "context.modules".
 */
WpClientContext * wp_client_context_new (WpCore * core, WpSpaJson * args,
    GError ** error);

/*
 * Returns the client context registered on \a core, or on its parent core,
 * or NULL if the client-context component is not loaded. (transfer full)
 */
WpClientContext * wp_client_context_find (WpCore * core);

/*
 * The lock is recursive (pw_thread_loop uses a PTHREAD_MUTEX_RECURSIVE mutex),
 * so nesting these is safe.
 */
void wp_client_context_lock (WpClientContext * self);
void wp_client_context_unlock (WpClientContext * self);

/* TRUE when called from the client context's loop thread */
gboolean wp_client_context_in_thread (WpClientContext * self);

struct pw_context * wp_client_context_get_pw_context (WpClientContext * self);
struct pw_core * wp_client_context_get_pw_core (WpClientContext * self);

/*
 * Queues \a callback for execution on the main thread. Calls are dispatched in
 * the order in which they were queued, one per GMainContext iteration, at
 * G_PRIORITY_DEFAULT. If the context is destroyed before \a callback runs,
 * \a destroy is still called on \a data.
 *
 * Safe to call from either thread.
 */
void wp_client_context_invoke_main (WpClientContext * self,
    GSourceFunc callback, gpointer data, GDestroyNotify destroy);

/*
 * WpProxy integration (implemented in proxy.c)
 *
 * A WpProxy whose pw_proxy lives on the client context's connection must be
 * told about it, before wp_proxy_set_pw_proxy() is called. WpProxy then takes
 * the loop lock around every pw_proxy call it makes and hands the pw_proxy
 * events that arrive on the loop thread over to the main thread.
 *
 * Note that this also means the object that the proxy exports must have been
 * created in the same context: pw_core_export() ties the exported object's
 * own context and data loop to the connection it is exported on.
 */
void wp_proxy_set_client_context (WpProxy * self, WpClientContext * ctx);
WpClientContext * wp_proxy_get_client_context (WpProxy * self);

G_END_DECLS

#endif
