/* WirePlumber
 *
 * Copyright © 2021 Asymptotic
 *    @author Arun Raghavan <arun@asymptotic.io>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_MODULE_H__
#define __WIREPLUMBER_MODULE_H__

#include "object.h"
#include "core.h"
#include "defs.h"
#include "properties.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to be used as WpObjectFeatures on WpImplModule.
 * \ingroup wpimplmodule
 * \since 0.6.0
 */
typedef enum { /*< flags >*/
  /*! Loads the PipeWire module */
  WP_IMPL_MODULE_FEATURE_LOADED = (1 << 0),
} WpImplModuleFeatures;

/*!
 * \brief The WpImplModule GType
 * \since 0.4.2
 * \ingroup wpimplmodule
 */
#define WP_TYPE_IMPL_MODULE (wp_impl_module_get_type())
WP_API
G_DECLARE_FINAL_TYPE (WpImplModule, wp_impl_module, WP, IMPL_MODULE, WpObject);

WP_API
WpImplModule * wp_impl_module_new (WpCore * core, const gchar * name,
    const gchar * args, WpProperties * properties);

WP_API
const gchar * wp_impl_module_get_name (WpImplModule * self);

WP_API
const gchar * wp_impl_module_get_args (WpImplModule * self);

WP_API
WpProperties * wp_impl_module_get_properties (WpImplModule * self);

G_END_DECLS

#endif /* __WIREPLUMBER_MODULE_H__ */
