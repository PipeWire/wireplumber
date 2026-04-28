/* WirePlumber
 *
 * Copyright © 2026 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pipewire/permission.h>
#include <pipewire/pipewire.h>

#include "private/permission-manager.h"
#include "permission-manager.h"
#include "proxy-interfaces.h"
#include "object-manager.h"
#include "json-utils.h"
#include "error.h"
#include "core.h"
#include "log.h"

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-permission-manager")

/*! \defgroup wppermissionmanager WpPermissionManager */
/*!
 * \struct WpPermissionManager
 *
 * The WpPermissionManager class is in charge of updating automatically
 * permissions on interested objects every time they are added or removed for
 * a particular client.
 *
 * WpPermissionManager API.
 */

typedef struct _PermissionMatch PermissionMatch;
struct _PermissionMatch
{
  guint32 id;
  guint32 permissions;
  GClosure *closure;
  WpObjectInterest *interest;
  WpSpaJson *rules;
};

static guint
get_next_id ()
{
  static guint32 next_id = 0;
  g_atomic_int_inc (&next_id);
  return next_id;
}

static PermissionMatch *
permission_match_new (guint32 perms, GClosure *closure,
    WpObjectInterest * interest, WpSpaJson * rules)
{
  PermissionMatch *match = g_new0 (PermissionMatch, 1);
  match->id = get_next_id ();
  match->permissions = perms;
  match->closure = closure ? g_closure_ref (closure) : NULL;
  match->interest = interest ? wp_object_interest_ref (interest) : NULL;
  match->rules = rules ? wp_spa_json_ref (rules) : NULL;
  return match;
}

static void
permission_interest_free (PermissionMatch *self)
{
  g_clear_pointer (&self->closure, g_closure_unref);
  g_clear_pointer (&self->interest, wp_object_interest_unref);
  g_clear_pointer (&self->rules, wp_spa_json_unref);
  g_free (self);
}

struct _WpPermissionManager
{
  WpObject parent;

  guint32 default_perms;
  guint32 core_perms;
  GPtrArray *clients;
  GHashTable *matches;

  WpObjectManager *om;
};

G_DEFINE_TYPE (WpPermissionManager, wp_permission_manager, WP_TYPE_OBJECT)

static void
wp_permission_manager_init (WpPermissionManager * self)
{
  /* Init default permissions to all */
  self->default_perms = PW_PERM_R | PW_PERM_W | PW_PERM_X;

  /* Core permissions not set by default (inherit from default_perms) */
  self->core_perms = PW_PERM_INVALID;

  /* Init permission interests table */
  self->matches = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify)permission_interest_free);

  /* Init clients list */
  self->clients = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_object_unref);
}

enum {
  STEP_LOAD = WP_TRANSITION_STEP_CUSTOM_START,
};

static WpObjectFeatures
wp_permission_manager_get_supported_features (WpObject * self)
{
  return WP_PERMISSION_MANAGER_LOADED;
}

static guint
wp_permission_manager_activate_get_next_step (WpObject * self,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  g_return_val_if_fail (missing == WP_PERMISSION_MANAGER_LOADED,
      WP_TRANSITION_STEP_ERROR);

  return STEP_LOAD;
}

static guint32
invoke_permissions_closure (WpPermissionManager *self, WpClient *client,
    WpGlobalProxy *object, GClosure *closure)
{
  GValue args[3] = { G_VALUE_INIT, G_VALUE_INIT, G_VALUE_INIT };
  GValue ret = G_VALUE_INIT;
  guint32 perms;

  g_value_init (&args[0], WP_TYPE_PERMISSION_MANAGER);
  g_value_set_object (&args[0], self);
  g_value_init (&args[1], WP_TYPE_CLIENT);
  g_value_set_object (&args[1], client);
  g_value_init (&args[2], WP_TYPE_GLOBAL_PROXY);
  g_value_set_object (&args[2], object);
  g_value_init (&ret, G_TYPE_UINT);

  g_closure_invoke (closure, &ret, 3, args, NULL);
  perms = g_value_get_uint (&ret);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);
  g_value_unset (&args[2]);
  g_value_unset (&ret);

  return perms;
}

typedef struct _MatchRulesCallbackData MatchRulesCallbackData;
struct _MatchRulesCallbackData {
  gboolean matched;
  guint32 perms;
};

static gboolean
match_rules_cb (gpointer data, const gchar * action, WpSpaJson * value,
    GError ** e)
{
  MatchRulesCallbackData *cb_data = (MatchRulesCallbackData *)data;
  g_autofree gchar *perms_str = NULL;
  guint32 perms = 0;

  if (!g_str_equal (action, "set-permissions")) {
    if (e)
      g_set_error (e, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "Action name '%s' is not valid", action);
    return FALSE;
  }

  if (!wp_spa_json_is_string (value)) {
    if (e)
      g_set_error (e, WP_DOMAIN_LIBRARY, WP_LIBRARY_ERROR_INVALID_ARGUMENT,
          "Action '%s' must be a string", action);
    return FALSE;
  }

  /* Parse permissions */
  perms_str = wp_spa_json_parse_string (value);
  if (g_strcmp0 (perms_str, "all") == 0) {
    perms = PW_PERM_ALL;
  } else if (perms_str) {
    for (guint i = 0; i < strlen (perms_str); i++) {
      switch (perms_str[i]) {
        case 'r': perms |= PW_PERM_R; break;
        case 'w': perms |= PW_PERM_W; break;
        case 'x': perms |= PW_PERM_X; break;
        case 'm': perms |= PW_PERM_M; break;
        case 'l': perms |= PW_PERM_L; break;
        case '-': break;
        default: {
          if (e)
            g_set_error (e, WP_DOMAIN_LIBRARY,
                WP_LIBRARY_ERROR_INVALID_ARGUMENT,
                "Permissions '%s' are not valid", perms_str);
          return FALSE;
        }
      }
    }
  }

  if (cb_data) {
    cb_data->matched = TRUE;
    cb_data->perms |= perms;
  }
  return TRUE;
}

static gboolean
get_rules_matched_object_permissions (WpPermissionManager *self,
    WpSpaJson *rules, WpGlobalProxy *object, guint32 *perms)
{
  g_autoptr (GError) e = NULL;
  g_autoptr (WpProperties) gp_props = NULL;
  g_autoptr (WpProperties) po_props = NULL;
  MatchRulesCallbackData data = { FALSE, 0 };

  /* Check global proxy properties */
  gp_props = wp_global_proxy_get_global_properties (object);
  if (gp_props && !wp_json_utils_match_rules (rules, gp_props, match_rules_cb,
      &data, &e))
    goto error;

  /* Also check pipewire object properties if it is a pipewire object */
  if (WP_IS_PIPEWIRE_OBJECT (object)) {
    po_props = wp_pipewire_object_get_properties (WP_PIPEWIRE_OBJECT (object));
    if (po_props && !wp_json_utils_match_rules (rules, po_props, match_rules_cb,
        &data, &e))
      goto error;
  }

  /* Set permissions if there was a match */
  if (data.matched && perms)
    *perms = data.perms;

  return data.matched;

error:
  wp_warning_object (self, "Malformed JSON match rules: %s", e->message);
  return FALSE;
}

static gboolean
get_matched_object_permissions (WpPermissionManager *self, PermissionMatch *m,
    WpClient *client, WpGlobalProxy *object, guint32 *perms)
{
  /* Check interest */
  if (m->interest && wp_object_interest_matches (m->interest, object)) {
    if (!perms)
      return TRUE;
    *perms = m->closure ? invoke_permissions_closure (self, client, object,
        m->closure) : m->permissions;
    return TRUE;
  }

  /* Check rules */
  if (m->rules)
    return get_rules_matched_object_permissions (self, m->rules, object, perms);

  return FALSE;
}

static GArray *
build_permissions_array (WpPermissionManager *self, WpClient *client)
{
  g_autoptr (WpIterator) it = NULL;
  g_auto (GValue) value = G_VALUE_INIT;
  struct pw_permission def_perm = { PW_ID_ANY, self->default_perms };
  GArray *arr = g_array_new (FALSE, FALSE, sizeof (struct pw_permission));

  /* Add default permissions */
  g_array_append_val (arr, def_perm);

  /* Add core permissions if explicitly set (core is not in the OM since it is
   * implicit in the PipeWire connection and not sent through the registry) */
  if (self->core_perms != PW_PERM_INVALID) {
    struct pw_permission core_perm = { PW_ID_CORE, self->core_perms };
    g_array_append_val (arr, core_perm);
  }

  /* Add object specific permissions in the array */
  it = wp_object_manager_new_iterator (self->om);
  for (; wp_iterator_next (it, &value); g_value_unset (&value)) {
    WpGlobalProxy *object = g_value_get_object (&value);
    GHashTableIter iter;
    PermissionMatch *match = NULL;
    g_hash_table_iter_init (&iter, self->matches);
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&match)) {
      guint32 perms = PW_PERM_INVALID;
      if (get_matched_object_permissions (self, match, client, object, &perms)
          && perms != PW_PERM_INVALID) {
        struct pw_permission obj_perm = { 0, };
        obj_perm.id = wp_proxy_get_bound_id (WP_PROXY (object));
        obj_perm.permissions = perms;
        g_array_append_val (arr, obj_perm);;
      }
    }
  }

  /* Merge permissions with same object ID */
  for (guint i = 0; i < arr->len; i++) {
    for (guint j = i + 1; j < arr->len; ) {
      struct pw_permission *a = &g_array_index (arr, struct pw_permission, i);
      struct pw_permission *b = &g_array_index (arr, struct pw_permission, j);
      if (a->id == b->id) {
        a->permissions |= b->permissions;
        g_array_remove_index (arr, j);
      } else {
        j++;
      }
    }
  }

  return arr;
}

static void
update_client_permissions (WpPermissionManager *self, WpClient *client)
{
  guint32 bound_id = 0;
  g_autoptr (GArray) perms = NULL;

  /* Dont do anything if the permission manager is not activated */
  if (!(wp_object_get_active_features (WP_OBJECT (self)) &
      WP_PERMISSION_MANAGER_LOADED))
    return;

  /* Make sure the client proxy is still valid */
  if (!wp_proxy_get_pw_proxy (WP_PROXY (client)))
    return;

  bound_id = wp_proxy_get_bound_id (WP_PROXY (client));
  perms = build_permissions_array (self, client);

  wp_info_object (self,
      "Updating permissions on client %u: any=%c%c%c%c%c len=%u",
      bound_id,
      !!(self->default_perms & PW_PERM_R) ? 'r' : '-',
      !!(self->default_perms & PW_PERM_W) ? 'w' : '-',
      !!(self->default_perms & PW_PERM_X) ? 'x' : '-',
      !!(self->default_perms & PW_PERM_M) ? 'm' : '-',
      !!(self->default_perms & PW_PERM_L) ? 'l' : '-',
      perms->len);

  wp_client_update_permissions_array (client, perms->len,
      (const struct pw_permission *) perms->data);
}

static gboolean
has_object_match (WpPermissionManager *self, WpGlobalProxy *object)
{
  GHashTableIter iter;
  PermissionMatch *m = NULL;

  g_hash_table_iter_init (&iter, self->matches);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&m)) {
    if (m->interest && wp_object_interest_matches (m->interest, object))
      return TRUE;
    if (m->rules && get_rules_matched_object_permissions (self, m->rules,
        object, NULL))
      return TRUE;
  }

  return FALSE;
}

static void
update_permissions (WpPermissionManager *self)
{
  for (guint i = 0; i < self->clients->len; i++) {
    WpClient *client = g_ptr_array_index (self->clients, i);
    update_client_permissions (self, client);
  }
}

static void
on_object_added_or_removed (WpObjectManager *om, WpGlobalProxy *object,
    gpointer d)
{
  WpPermissionManager * self = WP_PERMISSION_MANAGER (d);

  if (has_object_match (self, object))
    update_permissions (self);
}

static void
on_object_manager_installed (WpObjectManager *om, gpointer d)
{
  WpTransition * transition = WP_TRANSITION (d);
  WpPermissionManager * self = wp_transition_get_source_object (transition);

  wp_object_update_features (WP_OBJECT (self), WP_PERMISSION_MANAGER_LOADED, 0);
}

static void
wp_permission_manager_activate_execute_step (WpObject * object,
    WpFeatureActivationTransition * transition, guint step,
    WpObjectFeatures missing)
{
  WpPermissionManager *self = WP_PERMISSION_MANAGER (object);
  g_autoptr (WpCore) core = wp_object_get_core (object);

  switch (step) {
    case STEP_LOAD: {
      /* Install object manager */
      g_clear_object (&self->om);
      self->om = wp_object_manager_new ();
      wp_object_manager_add_interest (self->om, WP_TYPE_GLOBAL_PROXY, NULL);
      wp_object_manager_request_object_features (self->om,
          WP_TYPE_GLOBAL_PROXY, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
      g_signal_connect_object (self->om, "object-added",
          G_CALLBACK (on_object_added_or_removed), self, 0);
      g_signal_connect_object (self->om, "object-removed",
          G_CALLBACK (on_object_added_or_removed), self, 0);
      g_signal_connect_object (self->om, "installed",
          G_CALLBACK (on_object_manager_installed), transition, 0);
      wp_core_install_object_manager (core, self->om);
      break;
    }

    case WP_TRANSITION_STEP_ERROR:
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
wp_permission_manager_deactivate (WpObject * object, WpObjectFeatures features)
{
  WpPermissionManager *self = WP_PERMISSION_MANAGER (object);

  g_clear_object (&self->om);

  wp_object_update_features (WP_OBJECT (self), 0, WP_OBJECT_FEATURES_ALL);
}

static void
wp_permission_manager_finalize (GObject * object)
{
  WpPermissionManager *self = WP_PERMISSION_MANAGER (object);

  g_clear_pointer (&self->clients, g_ptr_array_unref);
  g_clear_pointer (&self->matches, g_hash_table_unref);

  g_clear_object (&self->om);

  G_OBJECT_CLASS (wp_permission_manager_parent_class)->finalize (object);
}

static void
wp_permission_manager_class_init (WpPermissionManagerClass * klass)
{
  GObjectClass * object_class = (GObjectClass *) klass;
  WpObjectClass *wpobject_class = (WpObjectClass *) klass;

  object_class->finalize = wp_permission_manager_finalize;

  wpobject_class->get_supported_features =
      wp_permission_manager_get_supported_features;
  wpobject_class->activate_get_next_step =
      wp_permission_manager_activate_get_next_step;
  wpobject_class->activate_execute_step =
      wp_permission_manager_activate_execute_step;
  wpobject_class->deactivate = wp_permission_manager_deactivate;
}

void
wp_permission_manager_add_client (WpPermissionManager *self, WpClient *client)
{
  g_return_if_fail (WP_IS_PERMISSION_MANAGER (self));

  g_ptr_array_add (self->clients, g_object_ref (client));
  update_client_permissions (self, client);
}

void
wp_permission_manager_remove_client (WpPermissionManager *self,
  WpClient *client)
{
  g_return_if_fail (WP_IS_PERMISSION_MANAGER (self));

  g_ptr_array_remove_fast (self->clients, client);
  update_client_permissions (self, client);
}

/*!
 * \brief Creates a new WpPermissionManager object
 *
 * \ingroup wppermissionmanager
 * \param core the WpCore
 * \returns (transfer full): a new WpPermissionManager object
 */
WpPermissionManager *
wp_permission_manager_new (WpCore * core)
{
  g_return_val_if_fail (core, NULL);

  return g_object_new (WP_TYPE_PERMISSION_MANAGER, "core", core, NULL);
}

/*!
 * \brief Sets the default permissions that will be applied to all objects that
 * don't match any interest
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 * \param permissions the default permissions to apply
 */
void
wp_permission_manager_set_default_permissions (WpPermissionManager *self,
    guint32 permissions)
{
  g_return_if_fail (WP_IS_PERMISSION_MANAGER (self));

  if (self->default_perms != permissions) {
    self->default_perms = permissions;
    update_permissions (self);
  }
}

/*!
 * \brief Sets the permissions that will be applied to the core object (ID 0).
 *
 * The core object is not visible to the permission manager's object manager
 * because it is implicit in the PipeWire connection and not sent through the
 * registry. This method allows setting explicit permissions on it, independent
 * of the default permissions.
 *
 * If not set (or set to PW_PERM_INVALID), the core inherits default_permissions.
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 * \param permissions the permissions to apply to the core object
 */
void
wp_permission_manager_set_core_permissions (WpPermissionManager *self,
    guint32 permissions)
{
  g_return_if_fail (WP_IS_PERMISSION_MANAGER (self));

  if (self->core_perms != permissions) {
    self->core_perms = permissions;
    update_permissions (self);
  }
}

static guint32
wp_permission_manager_add_match (WpPermissionManager *self,
    PermissionMatch *match)
{
  guint id = match->id;
  g_hash_table_insert (self->matches, GUINT_TO_POINTER (id), match);
  update_permissions (self);
  return id;
}

/*!
 * \brief Adds an interest match to apply permissions with callback in matched
 * objects.
 *
 * Interest consists of a GType that the object must be an ancestor of
 * (g_type_is_a() must match) and optionally, a set of additional constraints
 * on certain properties of the object. Refer to WpObjectInterest for more details.
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 * \param callback (scope async): the permissions match callback
 * \param user_data data to pass to \a callback
 * \param interest (transfer full): the interest
 * \returns the added match ID, or SPA_ID_INVALID if error
 */
guint32
wp_permission_manager_add_interest_match (WpPermissionManager *self,
    WpPermissionMatchCallback callback, gpointer user_data,
    WpObjectInterest * interest)
{
  GClosure *closure = g_cclosure_new (G_CALLBACK (callback), user_data, NULL);
  return wp_permission_manager_add_interest_match_closure (self, closure,
      interest);
}

/*!
 * \brief Adds an interest match to apply permissions with closure in matched
 * objects.
 *
 * Interest consists of a GType that the object must be an ancestor of
 * (g_type_is_a() must match) and optionally, a set of additional constraints
 * on certain properties of the object. Refer to WpObjectInterest for more details.
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 * \param closure (transfer full): the closure to apply permissions
 * \param interest (transfer full): the interest
 * \returns the added match ID, or SPA_ID_INVALID if error
 */
guint32
wp_permission_manager_add_interest_match_closure (WpPermissionManager *self,
    GClosure *closure, WpObjectInterest * interest)
{
  g_autoptr (WpObjectInterest) i = interest;
  g_autoptr (GClosure) c = closure;
  PermissionMatch *match;

  g_return_val_if_fail (WP_IS_PERMISSION_MANAGER (self), SPA_ID_INVALID);
  g_return_val_if_fail (closure, SPA_ID_INVALID);
  g_return_val_if_fail (i, SPA_ID_INVALID);

  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    g_closure_set_marshal (closure, g_cclosure_marshal_generic);

  match = permission_match_new (PW_PERM_INVALID, c, i, NULL);
  return wp_permission_manager_add_match (self, match);
}

/*!
 * \brief Adds an interest match to apply same permissions in matched objects.
 *
 * Interest consists of a GType that the object must be an ancestor of
 * (g_type_is_a() must match) and optionally, a set of additional constraints
 * on certain properties of the object. Refer to WpObjectInterest for more details.
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 * \param permissions the permissions to apply
 * \param interest (transfer full): the interest
 * \returns the added match ID, or SPA_ID_INVALID if error
 */
guint32
wp_permission_manager_add_interest_match_simple (WpPermissionManager *self,
    guint32 permissions, WpObjectInterest * interest)
{
  g_autoptr (WpObjectInterest) i = interest;
  PermissionMatch *match;

  g_return_val_if_fail (WP_IS_PERMISSION_MANAGER (self), SPA_ID_INVALID);
  g_return_val_if_fail (i, SPA_ID_INVALID);

  match = permission_match_new (permissions, NULL, i, NULL);
  return wp_permission_manager_add_match (self, match);
}

/*!
 * \brief Adds a rules match to apply permissions in matched objects.
 *
 * The rules must be defined in a JSON object using the same format as all
 * the wireplumber/pipewire rules.
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 * \param rules (transfer full): the JSON rules
 * \returns the added match ID, or SPA_ID_INVALID if error
 */
guint32
wp_permission_manager_add_rules_match (WpPermissionManager *self,
    WpSpaJson *rules)
{
  g_autoptr (WpSpaJson) r = rules;
  PermissionMatch *match;

  g_return_val_if_fail (WP_IS_PERMISSION_MANAGER (self), SPA_ID_INVALID);
  g_return_val_if_fail (r, SPA_ID_INVALID);

  match = permission_match_new (PW_PERM_INVALID, NULL, NULL, rules);
  return wp_permission_manager_add_match (self, match);
}

/*!
 * \brief Removes the previously added match so that the associated permissions
 * are not applied anymore.
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 * \param match_id the match ID to remove
 */
void
wp_permission_manager_remove_match (WpPermissionManager *self, guint32 match_id)
{
  g_return_if_fail (WP_IS_PERMISSION_MANAGER (self));
  g_return_if_fail (match_id != SPA_ID_INVALID);

  g_hash_table_remove (self->matches, GUINT_TO_POINTER (match_id));
  update_permissions (self);
}

/*!
 * \brief Updates permissions on all clients the permission manager has.
 *
 * The permission manager already updates permissions on all clients
 * automatically when a new client or object is added, however, this might be
 * needed if interests with closures or callbacks were added and something
 * changed externally.
 *
 * \ingroup wppermissionmanager
 * \param self the permission manager
 */
void
wp_permission_manager_update_permissions (WpPermissionManager *self)
{
  g_return_if_fail (WP_IS_PERMISSION_MANAGER (self));

  update_permissions (self);
}
