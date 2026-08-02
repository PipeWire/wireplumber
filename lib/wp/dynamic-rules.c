/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/pipewire.h>

#include "dynamic-rules.h"
#include "global-proxy.h"
#include "json-utils.h"
#include "object-manager.h"
#include "proxy-interfaces.h"
#include "log.h"
#include "error.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-dynamic-rules")

/*! \defgroup wpdynamicrules WpDynamicRules */
/*!
 * \struct WpDynamicRules
 *
 * WpDynamicRules evaluates dynamic rules against added subject objects and
 * triggers the \c apply-actions and \c revert-actions signals whenever a
 * rule's actions should be applied or reverted respectively.
 *
 * Rules can be added either from JSON with wp_dynamic_rules_add_json_rule()
 * or programmatically with wp_dynamic_rules_add_condition_rule() and
 * wp_dynamic_rules_add_condition_rule_closure(). JSON rules may carry an
 * optional \c conditions block in addition to the standard \c matches and
 * \c actions blocks. A condition block lists external objects that must
 * exist for the rule to be satisfied.
 *
 * Rule JSON format:
 * \verbatim
 * {
 *   matches = [ { <property> = <value> ... } ... ]
 *   conditions = [
 *     {
 *       matches = [ { <property> = <value> ... } ... ]
 *     }
 *     ...
 *   ]
 *   actions = { <action> = <value> ... }
 * }
 * \endverbatim
 *
 * Entries in \c conditions are AND: all must be satisfied. Entries in a
 * condition's \c matches are OR: any one matching object satisfies it.
 * Rules without a \c conditions block are always considered satisfied.
 *
 * Use wp_object_activate() with #WP_DYNAMIC_RULES_LOADED to activate.
 * Call wp_dynamic_rules_add_object() to register subject objects to evaluate.
 */

static guint
get_next_rule_id (void)
{
  static guint32 next_id = 0;
  g_atomic_int_inc (&next_id);
  return next_id;
}

typedef struct {
  WpSpaJson *match_json;
  GClosure *closure;
} DynamicCondition;

static void
dynamic_condition_free (DynamicCondition *self)
{
  g_clear_pointer (&self->match_json, wp_spa_json_unref);
  g_clear_pointer (&self->closure, g_closure_unref);
  g_free (self);
}

static DynamicCondition *
dynamic_condition_new (WpSpaJson *match_json, GClosure *closure)
{
  DynamicCondition *self = g_new0 (DynamicCondition, 1);
  self->match_json = match_json ? wp_spa_json_ref (match_json) : NULL;
  self->closure = closure ? g_closure_ref (closure) : NULL;
  return self;
}

typedef struct {
  guint32 id;
  WpSpaJson *match_json;
  WpObjectInterest *match_interest;
  WpSpaJson *actions;
  GPtrArray *conditions;
} DynamicRule;

static void
dynamic_rule_free (DynamicRule *self)
{
  g_clear_pointer (&self->match_json, wp_spa_json_unref);
  g_clear_pointer (&self->match_interest, wp_object_interest_unref);
  g_clear_pointer (&self->actions, wp_spa_json_unref);
  g_clear_pointer (&self->conditions, g_ptr_array_unref);
  g_free (self);
}

static DynamicRule *
dynamic_rule_new (WpObjectInterest *match_interest, WpSpaJson *match_json,
    WpSpaJson *actions)
{
  DynamicRule *self = g_new0 (DynamicRule, 1);
  self->id = get_next_rule_id ();
  self->match_json = match_json ? wp_spa_json_ref (match_json) : NULL;
  self->match_interest = match_interest ?
      wp_object_interest_ref (match_interest) : NULL;
  self->actions = actions ? wp_spa_json_ref (actions) : NULL;
  self->conditions = g_ptr_array_new_with_free_func (
      (GDestroyNotify) dynamic_condition_free);
  return self;
}

typedef enum {
  WP_RULES_CONDITION_STATE_UNMATCHED = 0,
  WP_RULES_CONDITION_STATE_UNSATISFIED = 1,
  WP_RULES_CONDITION_STATE_SATISFIED = 2,
} WpRulesConditionState;

enum {
  SIGNAL_APPLY_ACTIONS,
  SIGNAL_REVERT_ACTIONS,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = {0};

struct _WpDynamicRules {
  WpObject parent;

  /* Activation */
  WpObjectManager *condition_om;

  GPtrArray *rules;
  GPtrArray *objects;
  GHashTable *state;
};

G_DEFINE_TYPE (WpDynamicRules, wp_dynamic_rules, WP_TYPE_OBJECT)

static void
wp_dynamic_rules_init (WpDynamicRules *self)
{
  self->rules = g_ptr_array_new_with_free_func (
    (GDestroyNotify) dynamic_rule_free);
  self->objects = g_ptr_array_new_with_free_func (g_object_unref);
  self->state = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_array_unref);
}

static void
wp_dynamic_rules_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
wp_dynamic_rules_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

enum {
  STEP_LOAD = WP_TRANSITION_STEP_CUSTOM_START,
};

static WpObjectFeatures
wp_dynamic_rules_get_supported_features (WpObject *object)
{
  return WP_DYNAMIC_RULES_LOADED;
}

static guint
wp_dynamic_rules_activate_get_next_step (WpObject *object,
    WpFeatureActivationTransition *transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_DYNAMIC_RULES_LOADED,
      WP_TRANSITION_STEP_ERROR);
  return STEP_LOAD;
}

static WpSpaJson *
build_match_rules (WpSpaJson *matches_json,
    WpSpaJson *actions_json)
{
  g_autoptr (WpSpaJsonBuilder) ab = NULL;
  g_autoptr (WpSpaJsonBuilder) ob = NULL;
  g_autoptr (WpSpaJson) rule_json = NULL;

  ab = wp_spa_json_builder_new_array ();

  ob = wp_spa_json_builder_new_object ();
  wp_spa_json_builder_add_property (ob, "matches");
  wp_spa_json_builder_add_json (ob, matches_json);
  wp_spa_json_builder_add_property (ob, "actions");
  wp_spa_json_builder_add_json (ob, actions_json);
  rule_json = wp_spa_json_builder_end (ob);

  wp_spa_json_builder_add_json (ab, rule_json);

  return wp_spa_json_builder_end (ab);
}

static DynamicCondition *
parse_condition (WpSpaJson *cond_json)
{
  g_autoptr (WpSpaJson) matches_json = NULL;
  DynamicCondition *cond = NULL;
  g_autoptr (WpSpaJson) check_actions = NULL;

  if (!wp_spa_json_object_get (cond_json,
      "matches", "J", &matches_json,
      NULL))
    return NULL;

  /* This built-in action will allow us to easily check if a condition is
   * satisfied or not by re-using the wp_json_utils_match_rules() API */
  check_actions = wp_spa_json_new_object ("_check", "b", TRUE, NULL);

  cond = dynamic_condition_new (build_match_rules (matches_json, check_actions),
      NULL);
  return cond;
}

static DynamicCondition *
parse_condition_closure (GClosure *closure)
{
  DynamicCondition *cond;

  g_return_val_if_fail (closure, NULL);

  cond = dynamic_condition_new (NULL, closure);

  return cond;
}

static DynamicRule *
parse_single_rule (WpDynamicRules *self, WpSpaJson *rule_json)
{
  g_autoptr (WpSpaJson) matches_json = NULL;
  g_autoptr (WpSpaJson) actions_json = NULL;
  g_autoptr (WpSpaJson) conditions_json = NULL;
  DynamicRule *rule = NULL;

  /* Make sure rule is properly formed */
  if (!wp_spa_json_is_object (rule_json) ||
      !wp_spa_json_object_get (rule_json,
        "matches", "J", &matches_json,
        "actions", "J", &actions_json,
        NULL)) {
    g_autofree gchar *str = wp_spa_json_to_string (rule_json);
    wp_warning_object (self, "skipping malformed rule: %s", str);
    return NULL;
  }

  /* Parse optional fields */
  wp_spa_json_object_get (rule_json, "conditions", "J", &conditions_json,
      NULL);

  /* Create the rule */
  rule = dynamic_rule_new (NULL,
      build_match_rules (matches_json, actions_json), actions_json);

  /* Parse the conditions and add them to the rule */
  if (conditions_json && wp_spa_json_is_array (conditions_json)) {
    g_autoptr (WpIterator) cit = NULL;
    g_auto (GValue) citem = G_VALUE_INIT;

    cit = wp_spa_json_new_iterator (conditions_json);
    for (; wp_iterator_next (cit, &citem); g_value_unset (&citem)) {
      WpSpaJson *cond_json = g_value_get_boxed (&citem);
      DynamicCondition *cond = parse_condition (cond_json);
      if (cond)
        g_ptr_array_add (rule->conditions, cond);
    }
  }

  return rule;
}

static GArray *
get_or_create_rule_state (WpDynamicRules *self, WpGlobalProxy *obj)
{
  GArray *rule_state = g_hash_table_lookup (self->state, obj);
  if (!rule_state) {
    rule_state = g_array_new (FALSE, TRUE, sizeof (WpRulesConditionState));
    g_array_set_size (rule_state, self->rules->len);
    g_hash_table_insert (self->state, obj, rule_state);
  }
  return rule_state;
}

static gboolean
condition_check_cb (gpointer data, const gchar *action, WpSpaJson *value,
    GError **error)
{
  if (g_str_equal (action, "_check")) {
    gboolean check_val = FALSE;
    if (wp_spa_json_is_boolean (value) &&
        wp_spa_json_parse_boolean (value, &check_val))
      *(gboolean *) data = check_val;
  }

  return TRUE;
}

static gboolean
any_match_check_cb (gpointer data, const gchar *action, WpSpaJson *value,
    GError **error)
{
  *(gboolean *) data = TRUE;

  /* Stop after first, we only need to know it matched at least once */
  return FALSE;
}

static gboolean
rules_match_properties (WpSpaJson *rules, WpProperties *props)
{
  gboolean matched = FALSE;
  g_return_val_if_fail (props, FALSE);
  wp_json_utils_match_rules (rules, props, any_match_check_cb, &matched, NULL);
  return matched;
}

static gboolean
condition_is_satisfied (WpDynamicRules *self, DynamicCondition *cond)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  it = wp_object_manager_new_iterator (self->condition_om);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpGlobalProxy *obj = g_value_get_object (&val);
    g_autoptr (WpProperties) props = NULL;
    gboolean found = FALSE;

    if (WP_IS_PIPEWIRE_OBJECT (obj))
      props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (obj));
    else
      props = wp_global_proxy_get_global_properties (obj);

    wp_json_utils_match_rules (cond->match_json, props, condition_check_cb,
          &found, NULL);
    if (found)
      return TRUE;
  }

  return FALSE;
}

static gboolean
condition_closure_is_satisfied (WpDynamicRules *self, DynamicCondition *cond,
    WpGlobalProxy *subject)
{
  GValue ret = G_VALUE_INIT;
  GValue values[3] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };
  gboolean satisfied;

  g_value_init (&ret, G_TYPE_BOOLEAN);

  g_value_init (&values[0], WP_TYPE_DYNAMIC_RULES);
  g_value_set_object (&values[0], self);
  g_value_init (&values[1], WP_TYPE_GLOBAL_PROXY);
  g_value_set_object (&values[1], subject);
  g_value_init (&values[2], WP_TYPE_OBJECT_MANAGER);
  g_value_set_object (&values[2], self->condition_om);

  g_closure_invoke (cond->closure, &ret, 3, values, NULL);
  satisfied = g_value_get_boolean (&ret);

  g_value_unset (&values[0]);
  g_value_unset (&values[1]);
  g_value_unset (&values[2]);
  g_value_unset (&ret);

  return satisfied;
}

static gboolean
rule_matches_subject (DynamicRule *rule, WpGlobalProxy *subject,
    WpProperties *props)
{
  if (rule->match_interest)
    return wp_object_interest_matches (rule->match_interest, subject);
  return rules_match_properties (rule->match_json, props);
}

static gboolean
rule_conditions_satisfied (WpDynamicRules *self, DynamicRule *rule,
  WpGlobalProxy *subject)
{
  /* Check all conditions on the rule; all must be satisfied */
  for (guint i = 0; i < rule->conditions->len; i++) {
    DynamicCondition *cond = g_ptr_array_index (rule->conditions, i);
    if (cond->match_json && !condition_is_satisfied (self, cond))
      return FALSE;
    if (cond->closure && !condition_closure_is_satisfied (self, cond, subject))
      return FALSE;
  }

  return TRUE;
}

static void
emit_actions (WpDynamicRules *self, guint rule_id, WpGlobalProxy *subject,
    WpSpaJson *actions, gboolean apply)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) item = G_VALUE_INIT;
  guint sig = apply ?
      signals[SIGNAL_APPLY_ACTIONS] : signals[SIGNAL_REVERT_ACTIONS];

  if (!actions || !wp_spa_json_is_object (actions))
    return;

  /* Iterate through the actions object and emit a signal for each action */
  it = wp_spa_json_new_iterator (actions);
  for (; wp_iterator_next (it, &item); g_value_unset (&item)) {
    WpSpaJson *key_json = g_value_get_boxed (&item);
    g_autofree gchar *action_name = wp_spa_json_parse_string (key_json);

    if (!action_name)
      continue;

    /* Get the next item which is the value for this action */
    g_value_unset (&item);
    if (!wp_iterator_next (it, &item))
      break;

    WpSpaJson *value = g_value_get_boxed (&item);
    g_signal_emit (self, sig, 0, rule_id, action_name, value, subject);
  }
}

static void
evaluate_object (WpDynamicRules *self, WpGlobalProxy *obj)
{
  GArray *rule_state;
  g_autoptr (WpProperties) props = NULL;

  /* Get state */
  rule_state = get_or_create_rule_state (self, obj);
  if (rule_state->len < self->rules->len)
    g_array_set_size (rule_state, self->rules->len);

  /* Get the properties */
  if (WP_IS_PIPEWIRE_OBJECT (obj))
    props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (obj));
  else
    props = wp_global_proxy_get_global_properties (obj);

  /* Check if the object matches the rules and conditions */
  for (guint i = 0; i < self->rules->len; i++) {
    DynamicRule *rule = g_ptr_array_index (self->rules, i);
    WpRulesConditionState prev = g_array_index (rule_state,
        WpRulesConditionState, i);
    WpRulesConditionState new_state;
    gboolean new_satisfied;

    if (prev == WP_RULES_CONDITION_STATE_UNMATCHED &&
      !rule_matches_subject (rule, obj, props))
      continue;

    new_satisfied = rule_conditions_satisfied (self, rule, obj);
    new_state = new_satisfied ? WP_RULES_CONDITION_STATE_SATISFIED :
        WP_RULES_CONDITION_STATE_UNSATISFIED;
    if (prev == WP_RULES_CONDITION_STATE_UNMATCHED || new_state != prev) {
      g_array_index (rule_state, WpRulesConditionState, i) = new_state;
      emit_actions (self, rule->id, obj, rule->actions, new_satisfied);
    }
  }
}

static void
evaluate_all_objects (WpDynamicRules *self)
{
  for (guint i = 0; i < self->objects->len; i++)
    evaluate_object (self, g_ptr_array_index (self->objects, i));
}

static void
on_conditions_changed (WpObjectManager *om, WpDynamicRules *self)
{
  evaluate_all_objects (self);
}

static void
on_condition_om_installed (WpObjectManager *om, WpTransition *transition)
{
  WpDynamicRules *self = wp_transition_get_source_object (transition);

  evaluate_all_objects (self);
  wp_object_update_features (WP_OBJECT (self), WP_DYNAMIC_RULES_LOADED, 0);
}

static void
wp_dynamic_rules_activate_execute_step (WpObject *object,
    WpFeatureActivationTransition *transition, guint step,
    WpObjectFeatures missing)
{
  WpDynamicRules *self = WP_DYNAMIC_RULES (object);
  g_autoptr (WpCore) core = wp_object_get_core (object);

  switch (step) {
    case STEP_LOAD: {
      /* Create and install the condition object manager */
      g_return_if_fail (!self->condition_om);
      self->condition_om = wp_object_manager_new ();
      wp_object_manager_add_interest (self->condition_om,
          WP_TYPE_GLOBAL_PROXY, NULL);
      wp_object_manager_request_object_features (self->condition_om,
          WP_TYPE_GLOBAL_PROXY, WP_OBJECT_FEATURES_ALL);
      g_signal_connect_object (self->condition_om, "objects-changed",
          G_CALLBACK (on_conditions_changed), self, 0);
      g_signal_connect_object (self->condition_om, "installed",
          G_CALLBACK (on_condition_om_installed), transition, 0);
      wp_core_install_object_manager (core, self->condition_om);
      break;
    }

    case WP_TRANSITION_STEP_ERROR:
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
wp_dynamic_rules_deactivate (WpObject *object, WpObjectFeatures features)
{
  WpDynamicRules *self = WP_DYNAMIC_RULES (object);

  /* Reset state */
  g_hash_table_remove_all (self->state);

  /* Activation */
  g_clear_object (&self->condition_om);

  wp_object_update_features (object, 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_dynamic_rules_finalize (GObject *object)
{
  WpDynamicRules *self = WP_DYNAMIC_RULES (object);

  g_clear_pointer (&self->rules, g_ptr_array_unref);
  g_clear_pointer (&self->objects, g_ptr_array_unref);
  g_clear_pointer (&self->state, g_hash_table_unref);

  G_OBJECT_CLASS (wp_dynamic_rules_parent_class)->finalize (object);
}

static void
wp_dynamic_rules_class_init (WpDynamicRulesClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_dynamic_rules_finalize;
  object_class->set_property = wp_dynamic_rules_set_property;
  object_class->get_property = wp_dynamic_rules_get_property;

  wpobject_class->get_supported_features =
      wp_dynamic_rules_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_dynamic_rules_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_dynamic_rules_activate_execute_step;
  wpobject_class->deactivate = wp_dynamic_rules_deactivate;

  signals[SIGNAL_APPLY_ACTIONS] = g_signal_new ("apply-actions",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_STRING, WP_TYPE_SPA_JSON,
      WP_TYPE_GLOBAL_PROXY);
  signals[SIGNAL_REVERT_ACTIONS] = g_signal_new ("revert-actions",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_STRING, WP_TYPE_SPA_JSON,
      WP_TYPE_GLOBAL_PROXY);
}

/*!
 * \brief Creates a new WpDynamicRules object.
 *
 * Call wp_object_activate() with #WP_DYNAMIC_RULES_LOADED to start watching
 * objects. Use wp_dynamic_rules_add_json_rule() or
 * wp_dynamic_rules_add_condition_rule() to add rules to the object.
 *
 * \ingroup wpdynamicrules
 * \param core the WpCore
 * \returns (transfer full): the new WpDynamicRules
 */
WpDynamicRules *
wp_dynamic_rules_new (WpCore *core)
{
  g_return_val_if_fail (WP_IS_CORE (core), NULL);

  return g_object_new (WP_TYPE_DYNAMIC_RULES,
      "core", core,
      NULL);
}

/*!
 * \brief Adds a dynamic rule with a condition callback.
 *
 * The provided \a matches interest determines if a subject object is matched.
 * The provided \a actions object is emitted when the rule is satisfied.
 * The provided \a callback is evaluated as an additional condition.
 *
 * \ingroup wpdynamicrules
 * \param self the dynamic rules object
 * \param matches (transfer none): object interest that matches subject objects
 * \param actions (transfer none): action object emitted by the rule
 * \param callback (scope async): callback to evaluate the condition
 * \param user_data data to pass to \a callback
 * \returns the added rule ID, or SPA_ID_INVALID on error
 */
guint32
wp_dynamic_rules_add_condition_rule (WpDynamicRules *self,
    WpObjectInterest *matches, WpSpaJson *actions,
    WpDynamicRulesConditionCallback callback, gpointer user_data)
{
  g_autoptr (GClosure) closure = NULL;

  g_return_val_if_fail (WP_IS_DYNAMIC_RULES (self), SPA_ID_INVALID);
  g_return_val_if_fail (matches, SPA_ID_INVALID);
  g_return_val_if_fail (actions && wp_spa_json_is_object (actions),
      SPA_ID_INVALID);
  g_return_val_if_fail (callback, SPA_ID_INVALID);

  closure = g_cclosure_new (G_CALLBACK (callback), user_data, NULL);

  return wp_dynamic_rules_add_condition_rule_closure (self, matches, actions,
      g_steal_pointer (&closure));
}

static void
remove_rule_state_index (WpDynamicRules *self, guint idx)
{
  for (guint i = 0; i < self->objects->len; i++) {
    WpGlobalProxy *obj = g_ptr_array_index (self->objects, i);
    GArray *rule_state = g_hash_table_lookup (self->state, obj);

    if (!rule_state || idx >= rule_state->len)
      continue;

    g_array_remove_index (rule_state, idx);
  }
}

static void
emit_revert_for_rule (WpDynamicRules *self, DynamicRule *rule, guint idx)
{
  for (guint i = 0; i < self->objects->len; i++) {
    WpGlobalProxy *obj = g_ptr_array_index (self->objects, i);
    GArray *rule_state = g_hash_table_lookup (self->state, obj);

    if (!rule_state || idx >= rule_state->len)
      continue;

    if (g_array_index (rule_state, WpRulesConditionState, idx) !=
        WP_RULES_CONDITION_STATE_SATISFIED)
      continue;

    emit_actions (self, rule->id, obj, rule->actions, FALSE);
  }
}

/*!
 * \brief Adds a dynamic rule with a condition closure.
 *
 * The provided \a matches interest determines if a subject object is matched.
 * The provided \a actions object is emitted when the rule is satisfied.
 * The provided \a closure is evaluated as an additional condition.
 *
 * \ingroup wpdynamicrules
 * \param self the dynamic rules object
 * \param matches (transfer none): object interest that matches subject objects
 * \param actions (transfer none): action object emitted by the rule
 * \param closure (transfer full): closure to evaluate the condition
 * \returns the added rule ID, or SPA_ID_INVALID on error
 */
guint32
wp_dynamic_rules_add_condition_rule_closure (WpDynamicRules *self,
    WpObjectInterest *matches, WpSpaJson *actions, GClosure *closure)
{
  g_autoptr (GClosure) c = closure;
  DynamicRule *rule;
  guint32 id;

  g_return_val_if_fail (WP_IS_DYNAMIC_RULES (self), SPA_ID_INVALID);
  g_return_val_if_fail (matches, SPA_ID_INVALID);
  g_return_val_if_fail (actions && wp_spa_json_is_object (actions),
      SPA_ID_INVALID);
  g_return_val_if_fail (c, SPA_ID_INVALID);

  if (G_UNLIKELY (!wp_object_interest_validate (matches, NULL)))
    return SPA_ID_INVALID;

  if (G_CLOSURE_NEEDS_MARSHAL (c))
    g_closure_set_marshal (c, g_cclosure_marshal_generic);

  rule = dynamic_rule_new (matches, NULL, actions);
  id = rule->id;
  if (id == SPA_ID_INVALID) {
    dynamic_rule_free (rule);
    return SPA_ID_INVALID;
  }
  g_ptr_array_add (rule->conditions, parse_condition_closure (c));
  g_ptr_array_add (self->rules, rule);

  if (wp_object_get_active_features (WP_OBJECT (self)) &
      WP_DYNAMIC_RULES_LOADED)
    evaluate_all_objects (self);

  return id;
}

/*!
 * \brief Adds a dynamic rule from a JSON object.
 *
 * The JSON object must have "matches" and "actions" properties. It may
 * optionally have a "conditions" array property.
 *
 * \ingroup wpdynamicrules
 * \param self the dynamic rules object
 * \param json_rule (transfer none): JSON object defining the rule
 * \returns the added rule ID, or SPA_ID_INVALID on error
 */
guint32
wp_dynamic_rules_add_json_rule (WpDynamicRules *self, WpSpaJson *json_rule)
{
  DynamicRule *rule;

  g_return_val_if_fail (WP_IS_DYNAMIC_RULES (self), SPA_ID_INVALID);
  g_return_val_if_fail (json_rule, SPA_ID_INVALID);

  rule = parse_single_rule (self, json_rule);
  if (!rule)
    return SPA_ID_INVALID;

  g_ptr_array_add (self->rules, rule);

  if (wp_object_get_active_features (WP_OBJECT (self)) &
      WP_DYNAMIC_RULES_LOADED)
    evaluate_all_objects (self);

  return rule->id;
}

/*!
 * \brief Removes a previously added dynamic rule.
 *
 * \ingroup wpdynamicrules
 * \param self the dynamic rules object
 * \param rule_id the rule ID returned by
 * wp_dynamic_rules_add_condition_rule() or wp_dynamic_rules_add_json_rule()
 */
void
wp_dynamic_rules_remove_rule (WpDynamicRules *self, guint32 rule_id)
{
  g_return_if_fail (WP_IS_DYNAMIC_RULES (self));
  g_return_if_fail (rule_id != SPA_ID_INVALID);

  for (guint i = 0; i < self->rules->len; i++) {
    DynamicRule *rule = g_ptr_array_index (self->rules, i);
    if (rule->id == rule_id) {
      if (wp_object_get_active_features (WP_OBJECT (self)) &
          WP_DYNAMIC_RULES_LOADED)
        emit_revert_for_rule (self, rule, i);

      remove_rule_state_index (self, i);
      g_ptr_array_remove_index (self->rules, i);
      return;
    }
  }
}

/*!
 * \brief Adds an object to be evaluated against the rules.
 *
 * If already loaded, the object is evaluated immediately and \c apply-actions
 * or \c revert-actions signals are emitted as appropriate.
 *
 * \ingroup wpdynamicrules
 * \param self the dynamic rules object
 * \param object (transfer none): the subject object to add
 */
void
wp_dynamic_rules_add_object (WpDynamicRules *self, WpGlobalProxy *object)
{
  g_return_if_fail (WP_IS_DYNAMIC_RULES (self));
  g_return_if_fail (WP_IS_GLOBAL_PROXY (object));

  g_ptr_array_add (self->objects, g_object_ref (object));

  /* Evaluate object right away if dynamic rules are loaded */
  if (wp_object_get_active_features (WP_OBJECT (self)) & WP_DYNAMIC_RULES_LOADED)
    evaluate_object (self, object);
}

/*!
 * \brief Removes a previously added object.
 *
 * \ingroup wpdynamicrules
 * \param self the dynamic rules object
 * \param object (transfer none): the subject object to remove
 */
void
wp_dynamic_rules_remove_object (WpDynamicRules *self, WpGlobalProxy *object)
{
  g_return_if_fail (WP_IS_DYNAMIC_RULES (self));
  g_return_if_fail (WP_IS_GLOBAL_PROXY (object));

  g_ptr_array_remove (self->objects, object);
}
