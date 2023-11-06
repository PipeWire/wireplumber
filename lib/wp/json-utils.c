/* WirePlumber
 *
 * Copyright © 2023 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "json-utils.h"
#include "error.h"
#include "log.h"

#include <pipewire/pipewire.h>
#include <spa/utils/result.h>

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-json-utils")

/*! \defgroup wpjsonutils Json Utilities */

struct match_rules_cb_data
{
  WpRuleMatchCallback callback;
  gpointer data;
  GError **error;
};

static int
match_rules_cb (void *data, const char *location, const char *action,
    const char *str, size_t len)
{
  struct match_rules_cb_data *cb_data = data;
  g_autoptr (WpSpaJson) json = wp_spa_json_new_from_stringn (str, len);
  return cb_data->callback (cb_data->data, action, json, cb_data->error) ? 0 : -EPIPE;
}

/*!
 * \brief Matches the given properties against a set of rules descriped in JSON
 * and calls the given callback to perform actions on a successful match.
 *
 * The given JSON should be an array of objects, where each object has a
 * "matches" and an "actions" property. The "matches" value should also be
 * an array of objects, where each object is a set of properties to match.
 * Inside such an object, all properties must match to consider a successful
 * match. However, if multiple objects are provided, only one object needs
 * to match.
 *
 * The "actions" value should be an object where the key is the action name
 * and the value can be any valid JSON. Both the action name and the value are
 * passed as-is on the \a callback.
 *
 * \verbatim
 * [
 *     {
 *         matches = [
 *             # any of the items in matches needs to match, if one does,
 *             # actions are emited.
 *             {
 *                 # all keys must match the value. ! negates. ~ starts regex.
 *                 <key> = <value>
 *                 ...
 *             }
 *             ...
 *         ]
 *         actions = {
 *             <action> = <value>
 *             ...
 *         }
 *     }
 * ]
 * \endverbatim
 *
 * \ingroup wpjsonutils
 * \param json a JSON array containing rules in the described format
 * \param match_props (transfer none): the properties to match against the rules
 * \param callback (scope call): a function to call for each action on a successful match
 * \param data (closure callback): data to be passed to \a callback
 * \param error (out)(optional): the error that occurred, if any
 * \returns FALSE if an error occurred, TRUE otherwise
 */
gboolean
wp_json_utils_match_rules (WpSpaJson *json, WpProperties *match_props,
    WpRuleMatchCallback callback, gpointer data, GError ** error)
{
  g_autoptr (GError) cb_error = NULL;
  struct match_rules_cb_data cb_data = { callback, data, &cb_error };

  int res = pw_conf_match_rules (wp_spa_json_get_data (json),
      wp_spa_json_get_size (json), NULL, wp_properties_peek_dict (match_props),
      match_rules_cb, &cb_data);

  if (res < 0) {
    if (cb_error)
      g_propagate_error (error, g_steal_pointer (&cb_error));
    else
      g_set_error (error, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_OPERATION_FAILED,
          "match rules error: %s", spa_strerror (res));
    return FALSE;
  }

  return TRUE;
}

struct update_props_cb_data
{
  WpProperties *props;
  gint count;
};

static gboolean
update_props_cb (gpointer data, const gchar * action, WpSpaJson * value,
    GError ** error)
{
  struct update_props_cb_data *cb_data = data;
  if (g_str_equal (action, "update-props"))
    cb_data->count += wp_properties_update_from_json (cb_data->props, value);
  return TRUE;
}

/*!
 * \brief Matches the given properties against a set of rules descriped in JSON
 * and updates the properties if the rule actions include the "update-props"
 * action.
 *
 * \ingroup wpjsonutils
 * \param json a JSON array containing rules in the format accepted by
 *    wp_json_utils_match_rules()
 * \param props (transfer none): the properties to match against the rules
 *    and also update, acting on the "update-props" action
 * \returns the number of properties that were updated
 */
gint
wp_json_utils_match_rules_update_properties (WpSpaJson *json, WpProperties *props)
{
  g_autoptr (GError) cb_error = NULL;
  struct update_props_cb_data cb_data = { props, 0 };

  wp_json_utils_match_rules (json, props, update_props_cb, &cb_data, &cb_error);
  if (cb_error)
    wp_notice ("%s", cb_error->message);

  return cb_data.count;
}
