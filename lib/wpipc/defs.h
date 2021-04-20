/* WirePlumber
 *
 * Copyright © 2021 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WPIPC_DEFS_H__
#define __WPIPC_DEFS_H__

#if defined(__GNUC__)
# define WPIPC_API_EXPORT extern __attribute__ ((visibility ("default")))
#else
# define WPIPC_API_EXPORT extern
#endif

#ifndef WPIPC_API
#  define WPIPC_API WPIPC_API_EXPORT
#endif

#ifndef WPIPC_PRIVATE_API
#  define WPIPC_PRIVATE_API __attribute__ ((deprecated ("Private API")))
#endif

#endif
