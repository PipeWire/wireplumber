/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __WIREPLUMBER_DYNAMIC_RULES_H__
#define __WIREPLUMBER_DYNAMIC_RULES_H__

#include "core.h"
#include "global-proxy.h"
#include "object-interest.h"
#include "spa-json.h"

G_BEGIN_DECLS

/*!
 * \brief Flags to be used as WpObjectFeatures for WpDynamicRules.
 * \ingroup wpdynamicrules
 */
typedef enum { /*< flags >*/
  /*! Loads the dynamic rules */
  WP_DYNAMIC_RULES_LOADED = (1 << 0),
} WpDynamicRulesFeatures;

/*!
 * \brief The WpDynamicRules GType
 * \ingroup wpdynamicrules
 */
#define WP_TYPE_DYNAMIC_RULES (wp_dynamic_rules_get_type ())
WP_API
G_DECLARE_FINAL_TYPE (WpDynamicRules, wp_dynamic_rules, WP, DYNAMIC_RULES,
    WpObject)

/*!
 * \brief callback to evaluate a condition for a dynamic rule
 *
 * \ingroup wpdynamicrules
 * \param self the dynamic rules object
 * \param subject the subject currently being evaluated
 * \param condition_om the internal condition object manager
 * \param user_data user data passed during registration
 * \retval TRUE if the condition is satisfied
 * \retval FALSE if the condition is not satisfied
 *
 * The callback is invoked whenever conditions are re-evaluated for the rule,
 * including when the internal condition object manager emits "objects-changed".
 */
typedef gboolean (*WpDynamicRulesConditionCallback) (WpDynamicRules *self,
    WpGlobalProxy *subject, WpObjectManager *condition_om, gpointer user_data);

WP_API
WpDynamicRules * wp_dynamic_rules_new (WpCore *core);

WP_API
guint32 wp_dynamic_rules_add_condition_rule (WpDynamicRules *self,
    WpObjectInterest *matches, WpSpaJson *actions,
    WpDynamicRulesConditionCallback callback, gpointer user_data);

WP_API
guint32 wp_dynamic_rules_add_condition_rule_closure (WpDynamicRules *self,
    WpObjectInterest *matches, WpSpaJson *actions, GClosure *closure);

WP_API
guint32 wp_dynamic_rules_add_json_rule (WpDynamicRules *self,
    WpSpaJson *json_rule);

WP_API
void wp_dynamic_rules_remove_rule (WpDynamicRules *self, guint32 rule_id);

WP_API
void wp_dynamic_rules_add_object (WpDynamicRules *self, WpGlobalProxy *object);

WP_API
void wp_dynamic_rules_remove_object (WpDynamicRules *self,
    WpGlobalProxy *object);

G_END_DECLS

#endif /* __WIREPLUMBER_DYNAMIC_RULES_H__ */
