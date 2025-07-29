/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WPLUA_PRIVATE_H__
#define __WPLUA_PRIVATE_H__

#include "wplua.h"

G_BEGIN_DECLS

#define WP_LOCAL_LOG_TOPIC log_topic_wplua
WP_LOG_TOPIC_EXTERN (log_topic_wplua)

/* boxed.c */
void _wplua_init_gboxed (lua_State *L);

/* closure.c */
void _wplua_init_closure (lua_State *L);

/* object.c */
void _wplua_init_gobject (lua_State *L);

/* userdata.c */
GValue * _wplua_pushgvalue_userdata (lua_State * L, GType type);
GValue * _wplua_togvalue_userdata_named (lua_State *L, int idx, GType type, const char *table_name);
GValue * _wplua_togvalue_userdata (lua_State *L, int idx, GType type);
gboolean _wplua_isgvalue_userdata (lua_State *L, int idx, GType type);
int _wplua_gvalue_userdata___eq_impl (lua_State *L, const char *type);
int _wplua_gvalue_userdata___gc (lua_State *L);

/* wplua.c */
int _wplua_pcall (lua_State *L, int nargs, int nret);

G_END_DECLS

#endif
