/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "../common/base-test-fixture.h"

typedef struct {
  WpBaseTestFixture base;
  WpGlobalProxy *subject_node;
  guint apply_count;
  guint revert_count;
  guint32 rule_id;
} TestFixture;

static gboolean
load_test_node_factory (TestFixture *f)
{
  g_autoptr (WpTestServerLocker) lock = NULL;

  lock = wp_test_server_locker_new (&f->base.server);

  g_assert_cmpint (pw_context_add_spa_lib (f->base.server.context,
          "audiotestsrc", "audiotestsrc/libspa-audiotestsrc"), ==, 0);
  if (!test_is_spa_lib_installed (&f->base, "audiotestsrc"))
    return FALSE;

  g_assert_nonnull (pw_context_load_module (f->base.server.context,
          "libpipewire-module-adapter", NULL, NULL));
  return TRUE;
}

static WpGlobalProxy *
create_client_node (TestFixture *f, const gchar *name)
{
  g_autoptr (WpGlobalProxy) node = NULL;

  node = WP_GLOBAL_PROXY (wp_node_new_from_factory (f->base.client_core,
      "adapter",
      wp_properties_new (
          "factory.name", "audiotestsrc",
          "node.name", name,
          NULL)));
  g_assert_nonnull (node);

  wp_object_activate (WP_OBJECT (node), WP_OBJECT_FEATURES_ALL,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);

  return g_steal_pointer (&node);
}

static void
on_apply_actions (WpDynamicRules *rules, guint rule_id, const gchar *action,
    WpSpaJson *value, WpGlobalProxy *subject, TestFixture *f)
{
  gboolean v = FALSE;

  g_assert_true (WP_IS_DYNAMIC_RULES (rules));
  g_assert_cmpuint (rule_id, ==, f->rule_id);
  g_assert_cmpstr (action, ==, "test_action");
  g_assert_true (WP_IS_GLOBAL_PROXY (subject));
  g_assert_true (subject == f->subject_node);
  g_assert_true (wp_spa_json_parse_boolean (value, &v));
  g_assert_true (v);

  f->apply_count++;

  g_main_loop_quit (f->base.loop);
}

static void
on_revert_actions (WpDynamicRules *rules, guint rule_id, const gchar *action,
    WpSpaJson *value, WpGlobalProxy *subject, TestFixture *f)
{
  gboolean v = FALSE;

  g_assert_true (WP_IS_DYNAMIC_RULES (rules));
  g_assert_cmpuint (rule_id, ==, f->rule_id);
  g_assert_cmpstr (action, ==, "test_action");
  g_assert_true (WP_IS_GLOBAL_PROXY (subject));
  g_assert_true (subject == f->subject_node);
  g_assert_true (wp_spa_json_parse_boolean (value, &v));
  g_assert_true (v);

  f->revert_count++;

  g_main_loop_quit (f->base.loop);
}

static void
test_dynamic_rules_setup (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_setup (&self->base, WP_BASE_TEST_FLAG_CLIENT_CORE);

  if (!load_test_node_factory (self)) {
    g_test_skip ("The pipewire audiotestsrc factory was not found");
    return;
  }

  self->subject_node = create_client_node (self,
        "test-dynamic-rules-subject");
}

static void
test_dynamic_rules_teardown (TestFixture *self, gconstpointer user_data)
{
  wp_base_test_fixture_teardown (&self->base);

  g_clear_object (&self->subject_node);
}

static gboolean
test_dynamic_rules_rule_condition (WpDynamicRules *rules, WpGlobalProxy *subject,
    WpObjectManager *condition_om, gpointer user_data)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) val = G_VALUE_INIT;

  /* Check if there's an object with node.name = "test-dynamic-rules-trigger" */
  it = wp_object_manager_new_iterator (condition_om);
  for (; wp_iterator_next (it, &val); g_value_unset (&val)) {
    WpGlobalProxy *obj = g_value_get_object (&val);
    g_autoptr (WpProperties) props = NULL;

    if (WP_IS_PIPEWIRE_OBJECT (obj))
      props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (obj));
    else
      props = wp_global_proxy_get_global_properties (obj);

    if (props) {
      const gchar *name = wp_properties_get (props, "node.name");
      if (name && g_str_equal (name, "test-dynamic-rules-trigger"))
        return TRUE;
    }
  }

  return FALSE;
}

static void
test_dynamic_rules_condition_rule (TestFixture *f, gconstpointer user_data)
{
  g_autoptr (WpGlobalProxy) trigger_node = NULL;
  g_autoptr (WpObjectInterest) matches = NULL;
  g_autoptr (WpSpaJson) actions = NULL;
  g_autoptr (WpDynamicRules) dr = NULL;
  guint32 rule_id;

  /* Create the object interest for subject match */
  matches = wp_object_interest_new_type (WP_TYPE_GLOBAL_PROXY);
  wp_object_interest_add_constraint (matches, WP_CONSTRAINT_TYPE_PW_PROPERTY,
      "node.name", WP_CONSTRAINT_VERB_EQUALS,
      g_variant_new_string ("test-dynamic-rules-subject"));

  /* Create the actions JSON object */
  actions = wp_spa_json_new_object ("test_action", "b", TRUE, NULL);
  g_assert_nonnull (actions);

  /* Create the dynamic rules and handle its signals */
  dr = wp_dynamic_rules_new (f->base.core);
  g_signal_connect (dr, "apply-actions", G_CALLBACK (on_apply_actions), f);
  g_signal_connect (dr, "revert-actions", G_CALLBACK (on_revert_actions), f);

  /* Add the rule with a condition callback */
  rule_id = wp_dynamic_rules_add_condition_rule (dr, matches, actions,
      test_dynamic_rules_rule_condition, NULL);
  g_assert_cmpuint (rule_id, !=, SPA_ID_INVALID);
  f->rule_id = rule_id;

  /* Activate the dynamic rules */
  wp_object_activate (WP_OBJECT (dr), WP_DYNAMIC_RULES_LOADED,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (f->apply_count, ==, 0);
  g_assert_cmpuint (f->revert_count, ==, 0);

  /* Add subject and check revert is triggered as condition is not satisfied */
  wp_dynamic_rules_add_object (dr, f->subject_node);
  g_assert_cmpuint (f->apply_count, ==, 0);
  g_assert_cmpuint (f->revert_count, ==, 1);

  /* Add object to satisfy condition and make sure apply is triggered */
  trigger_node = create_client_node (f, "test-dynamic-rules-trigger");
  g_assert_nonnull (trigger_node);
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (f->apply_count, ==, 1);
  g_assert_cmpuint (f->revert_count, ==, 1);

  /* Destroy the trigger node and make sure revert is triggered again */
  g_clear_object (&trigger_node);
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (f->apply_count, ==, 1);
  g_assert_cmpuint (f->revert_count, ==, 2);
}

static void
test_dynamic_rules_json_rule (TestFixture *f, gconstpointer user_data)
{
  g_autoptr (WpGlobalProxy) trigger_node = NULL;
  g_autoptr (WpSpaJson) rule_json = NULL;
  g_autoptr (WpDynamicRules) dr = NULL;
  guint32 rule_id;

  /* Build the JSON rule */
  {
    g_autoptr (WpSpaJsonBuilder) rule_b = wp_spa_json_builder_new_object ();
    g_autoptr (WpSpaJsonBuilder) matches_b = wp_spa_json_builder_new_array ();
    g_autoptr (WpSpaJsonBuilder) match_b = wp_spa_json_builder_new_object ();
    g_autoptr (WpSpaJsonBuilder) actions_b = wp_spa_json_builder_new_object ();
    g_autoptr (WpSpaJsonBuilder) conditions_b = wp_spa_json_builder_new_array ();
    g_autoptr (WpSpaJsonBuilder) cond_b = wp_spa_json_builder_new_object ();
    g_autoptr (WpSpaJsonBuilder) cond_matches_b = wp_spa_json_builder_new_array ();
    g_autoptr (WpSpaJsonBuilder) cond_match_b = wp_spa_json_builder_new_object ();
    g_autoptr (WpSpaJson) match_json = NULL;
    g_autoptr (WpSpaJson) cond_match_json = NULL;
    g_autoptr (WpSpaJson) cond_matches_json = NULL;
    g_autoptr (WpSpaJson) cond_json = NULL;
    g_autoptr (WpSpaJson) matches_json = NULL;
    g_autoptr (WpSpaJson) actions_json = NULL;
    g_autoptr (WpSpaJson) conditions_json = NULL;

    /* Build the subject match */
    wp_spa_json_builder_add_property (match_b, "node.name");
    wp_spa_json_builder_add_string (match_b, "test-dynamic-rules-subject");
    match_json = wp_spa_json_builder_end (match_b);
    wp_spa_json_builder_add_json (matches_b, match_json);
    matches_json = wp_spa_json_builder_end (matches_b);

    /* Build the condition match */
    wp_spa_json_builder_add_property (cond_match_b, "node.name");
    wp_spa_json_builder_add_string (cond_match_b, "test-dynamic-rules-trigger");
    cond_match_json = wp_spa_json_builder_end (cond_match_b);
    wp_spa_json_builder_add_json (cond_matches_b, cond_match_json);
    cond_matches_json = wp_spa_json_builder_end (cond_matches_b);

    /* Build the condition */
    wp_spa_json_builder_add_property (cond_b, "matches");
    wp_spa_json_builder_add_json (cond_b, cond_matches_json);
    cond_json = wp_spa_json_builder_end (cond_b);
    wp_spa_json_builder_add_json (conditions_b, cond_json);
    conditions_json = wp_spa_json_builder_end (conditions_b);

    /* Build the actions */
    wp_spa_json_builder_add_property (actions_b, "test_action");
    wp_spa_json_builder_add_boolean (actions_b, TRUE);
    actions_json = wp_spa_json_builder_end (actions_b);

    /* Assemble the rule */
    wp_spa_json_builder_add_property (rule_b, "matches");
    wp_spa_json_builder_add_json (rule_b, matches_json);
    wp_spa_json_builder_add_property (rule_b, "conditions");
    wp_spa_json_builder_add_json (rule_b, conditions_json);
    wp_spa_json_builder_add_property (rule_b, "actions");
    wp_spa_json_builder_add_json (rule_b, actions_json);
    rule_json = wp_spa_json_builder_end (rule_b);
  }
  g_assert_nonnull (rule_json);

  /* Create the dynamic rules and handle its signals */
  dr = wp_dynamic_rules_new (f->base.core);
  g_signal_connect (dr, "apply-actions", G_CALLBACK (on_apply_actions), f);
  g_signal_connect (dr, "revert-actions", G_CALLBACK (on_revert_actions), f);

  /* Add the JSON rule */
  rule_id = wp_dynamic_rules_add_json_rule (dr, rule_json);
  g_assert_cmpuint (rule_id, !=, SPA_ID_INVALID);
  f->rule_id = rule_id;

  /* Activate the dynamic rules */
  wp_object_activate (WP_OBJECT (dr), WP_DYNAMIC_RULES_LOADED,
      NULL, (GAsyncReadyCallback) test_object_activate_finish_cb, &f->base);
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (f->apply_count, ==, 0);
  g_assert_cmpuint (f->revert_count, ==, 0);

  /* Add subject and check revert is triggered as condition is not satisfied */
  wp_dynamic_rules_add_object (dr, f->subject_node);
  g_assert_cmpuint (f->apply_count, ==, 0);
  g_assert_cmpuint (f->revert_count, ==, 1);

  /* Add object to satisfy condition and make sure apply is triggered */
  trigger_node = create_client_node (f, "test-dynamic-rules-trigger");
  g_assert_nonnull (trigger_node);
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (f->apply_count, ==, 1);
  g_assert_cmpuint (f->revert_count, ==, 1);

  /* Destroy the trigger node and make sure revert is triggered again */
  g_clear_object (&trigger_node);
  g_main_loop_run (f->base.loop);
  g_assert_cmpuint (f->apply_count, ==, 1);
  g_assert_cmpuint (f->revert_count, ==, 2);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/dynamic-rules/condition-rule", TestFixture, NULL,
      test_dynamic_rules_setup, test_dynamic_rules_condition_rule,
      test_dynamic_rules_teardown);

  g_test_add ("/wp/dynamic-rules/json-rule", TestFixture, NULL,
      test_dynamic_rules_setup, test_dynamic_rules_json_rule,
      test_dynamic_rules_teardown);

  return g_test_run ();
}
