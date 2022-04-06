/* WirePlumber
 *
 * Copyright © 2022 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __MODULE_LUA_SCRIPTING_SCRIPT_H__
#define __MODULE_LUA_SCRIPTING_SCRIPT_H__

#include <wp/wp.h>
#include <wplua/wplua.h>

G_BEGIN_DECLS

#define WP_TYPE_LUA_SCRIPT (wp_lua_script_get_type ())
G_DECLARE_FINAL_TYPE (WpLuaScript, wp_lua_script, WP, LUA_SCRIPT, WpPlugin)

G_END_DECLS

#endif
