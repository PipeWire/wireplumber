/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WP_ERROR_H__
#define __WP_ERROR_H__

#include <glib.h>
#include "defs.h"

G_BEGIN_DECLS

/**
 * WP_DOMAIN_LIBRARY:
 *
 * @file error.c
 * @struct ErrorCodes
 * @section error_section Error codes
 *
 * @brief Error domain and codes for <a href="https://developer.gnome.org/glib/stable/glib-Error-Reporting.html#GError">
 * GError</a>
 *
 * @b wp_domain_library_quark
 *
 * @code
 * GQuark wp_domain_library_quark ()
 * @endcode
 *
 * @subsection error_enum_subsection Enumerations
 *
 * @subsubsection error_enum_subsubsection WpLibraryErrorEnum:
 *
 * @brief Error codes that can appear in a 
 * <a href="https://developer.gnome.org/glib/stable/glib-Error-Reporting.html#GError">GError</a>
 * when the error domain is [WP_DOMAIN_LIBRARY](@ref error_constants_subsection)
 *
 * @b Members:
 *
 * @arg `WP_LIBRARY_ERROR_INVARIANT (0) – an invariant check failed; this most likely indicates a programming error`
 * @arg `WP_LIBRARY_ERROR_INVALID_ARGUMENT (1) – an unexpected/invalid argument was given`
 * @arg `WP_LIBRARY_ERROR_OPERATION_FAILED (2) – an operation failed`
 *
 * @subsection error_constants_subsection Constants
 *
 * @b WP_DOMAIN_LIBRARY:
 *
 * A <a href="https://developer.gnome.org/glib/stable/glib-Error-Reporting.html#GError">
 * GError</a> domain for errors that occurred within the context of the
 * WirePlumber library.
 *
 * @code
 * #define WP_DOMAIN_LIBRARY (wp_domain_library_quark ())
 * @endcode 
 */
#define WP_DOMAIN_LIBRARY (wp_domain_library_quark ())
WP_API
GQuark wp_domain_library_quark (void);

/*!
 * @em WP_LIBRARY_ERROR_INVARIANT: an invariant check failed; this most likely
 *    indicates a programming error
 * @em WP_LIBRARY_ERROR_INVALID_ARGUMENT: an unexpected/invalid argument was given
 * @em WP_LIBRARY_ERROR_OPERATION_FAILED: an operation failed
 *
 * Error codes that can appear in a 
 * <a href="https://developer.gnome.org/glib/stable/glib-Error-Reporting.html#GError">
 * GError</a> when the error domain
 * is %WP_DOMAIN_LIBRARY
 */
typedef enum {
  WP_LIBRARY_ERROR_INVARIANT,
  WP_LIBRARY_ERROR_INVALID_ARGUMENT,
  WP_LIBRARY_ERROR_OPERATION_FAILED,
} WpLibraryErrorEnum;

G_END_DECLS

#endif
