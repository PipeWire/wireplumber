/* WirePlumber
 *
 * Copyright © 2020 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>

#include "../../modules/module-monitor/dbus-device-reservation.h"

typedef struct {
  GTestDBus *dbus_test;
  GMainLoop *loop;
  gboolean acquired;
  gboolean released;
  gpointer property;
} TestDbusFixture;

static void
test_dbus_setup (TestDbusFixture *self, gconstpointer data)
{
  self->dbus_test = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (self->dbus_test);
  self->loop = g_main_loop_new (NULL, FALSE);
}

static void
test_dbus_teardown (TestDbusFixture *self, gconstpointer data)
{
  g_clear_pointer (&self->loop, g_main_loop_unref);
  g_test_dbus_down (self->dbus_test);
  g_clear_object (&self->dbus_test);
}

static void
on_reservation_release (WpMonitorDbusDeviceReservation *reservation,
    int forced, TestDbusFixture *self)
{
  wp_monitor_dbus_device_reservation_release (reservation);
  wp_monitor_dbus_device_reservation_complete_release (reservation, TRUE);
}

static void
on_acquired_done (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  TestDbusFixture *self = user_data;
  WpMonitorDbusDeviceReservation *r = WP_MONITOR_DBUS_DEVICE_RESERVATION (obj);
  g_autoptr (GError) e = NULL;
  wp_monitor_dbus_device_reservation_async_finish (r, res, &e);
  g_assert_null (e);
  self->acquired = TRUE;
  g_main_loop_quit (self->loop);
}

static void
on_request_release_done (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  TestDbusFixture *self = user_data;
  WpMonitorDbusDeviceReservation *r = WP_MONITOR_DBUS_DEVICE_RESERVATION (obj);
  g_autoptr (GError) e = NULL;
  wp_monitor_dbus_device_reservation_async_finish (r, res, &e);
  g_assert_null (e);
  self->released = TRUE;
  g_main_loop_quit (self->loop);
}

static void
on_request_property_done (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  TestDbusFixture *self = user_data;
  WpMonitorDbusDeviceReservation *r = WP_MONITOR_DBUS_DEVICE_RESERVATION (obj);
  g_autoptr (GError) e = NULL;
  gpointer ret = wp_monitor_dbus_device_reservation_async_finish (r, res, &e);
  g_assert_null (e);
  self->property = ret;
  g_main_loop_quit (self->loop);
}

static WpMonitorDbusDeviceReservation *
create_representation (TestDbusFixture *self, gint card_id,
    const char *app_name, gint priority, const char *app_dev_name)
{
  WpMonitorDbusDeviceReservation *r = wp_monitor_dbus_device_reservation_new (
      card_id, app_name, priority, app_dev_name);
  g_assert_nonnull (r);
  g_signal_connect (r, "release",
      (GCallback) on_reservation_release, self);
  return r;
}

static void
test_dbus_basic (TestDbusFixture *self, gconstpointer data)
{
  g_autoptr (WpMonitorDbusDeviceReservation) r1 = NULL;
  g_autoptr (WpMonitorDbusDeviceReservation) r2 = NULL;

  /* Create 2 reservations */
  r1 = create_representation (self, 0, "Server", 10, "hw:0,0");
  r2 = create_representation (self, 0, "PipeWire", 15, "hw:0,0");

  /* Acquire the device on r1 */
  self->acquired = FALSE;
  g_assert_true (wp_monitor_dbus_device_reservation_acquire (r1, NULL,
      on_acquired_done, self));
  g_main_loop_run (self->loop);
  g_assert_true (self->acquired);

  /* Request the priority property on r1 and make sure it is 10 */
  self->property = NULL;
  g_assert_true (wp_monitor_dbus_device_reservation_request_property (r1,
      "Priority", NULL, on_request_property_done, self));
  g_main_loop_run (self->loop);
  g_assert_nonnull (self->property);
  g_assert_cmpint (GPOINTER_TO_INT (self->property), ==, 10);

  /* Request the application name property on r1 and make sure it is Server */
  self->property = NULL;
  g_assert_true (wp_monitor_dbus_device_reservation_request_property (r1,
      "ApplicationName", NULL, on_request_property_done, self));
  g_main_loop_run (self->loop);
  g_assert_nonnull (self->property);
  g_assert_cmpstr (self->property, ==, "Server");

  /* Request the app device name property on r1 and make sure it is hw:0,0 */
  self->property = NULL;
  g_assert_true (wp_monitor_dbus_device_reservation_request_property (r1,
     "ApplicationDeviceName", NULL,  on_request_property_done, self));
  g_main_loop_run (self->loop);
  g_assert_nonnull (self->property);
  g_assert_cmpstr (self->property, ==, "hw:0,0");

  /* Request the priority property on r2 and make sure it is also 10 because r1
   * owns the device */
  self->property = NULL;
  g_assert_true (wp_monitor_dbus_device_reservation_request_property (r2,
      "Priority", NULL, on_request_property_done, self));
  g_main_loop_run (self->loop);
  g_assert_nonnull (self->property);
  g_assert_cmpint (GPOINTER_TO_INT (self->property), ==, 10);

  /* Request release on r2 (higher priority) */
  self->released = FALSE;
  g_assert_true (wp_monitor_dbus_device_reservation_request_release (r2, NULL,
      on_request_release_done, self));
  g_main_loop_run (self->loop);
  g_assert_true (self->released);

  /* Acquire the device on r2 */
  self->acquired = FALSE;
  g_assert_true (wp_monitor_dbus_device_reservation_acquire (r2, NULL,
      on_acquired_done, self));
  g_main_loop_run (self->loop);
  g_assert_true (self->acquired);

  /* Request the priority property on r2 and make sure it is 15 */
  self->property = NULL;
  g_assert_true (wp_monitor_dbus_device_reservation_request_property (r2,
      "Priority", NULL, on_request_property_done, self));
  g_main_loop_run (self->loop);
  g_assert_nonnull (self->property);
  g_assert_cmpint (GPOINTER_TO_INT (self->property), ==, 15);

  /* Request the application name property on r1 and make sure it is Server */
  self->property = NULL;
  g_assert_true (wp_monitor_dbus_device_reservation_request_property (r2,
      "ApplicationName", NULL, on_request_property_done, self));
  g_main_loop_run (self->loop);
  g_assert_nonnull (self->property);
  g_assert_cmpstr (self->property, ==, "PipeWire");

  /* Request the app device name property on r2 and make sure it is hw:0,0 */
  self->property = NULL;
  g_assert_true (wp_monitor_dbus_device_reservation_request_property (r2,
     "ApplicationDeviceName", NULL,  on_request_property_done, self));
  g_main_loop_run (self->loop);
  g_assert_nonnull (self->property);
  g_assert_cmpstr (self->property, ==, "hw:0,0");

  /* Request the priority property on r1 and make sure it is also 15 because r2
   * owns the device now */
  self->property = NULL;
  g_assert_true (wp_monitor_dbus_device_reservation_request_property (r1,
      "Priority", NULL, on_request_property_done, self));
  g_main_loop_run (self->loop);
  g_assert_nonnull (self->property);
  g_assert_cmpint (GPOINTER_TO_INT (self->property), ==, 15);
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  wp_init (WP_INIT_ALL);

  g_test_add ("/wp/dbus/basic", TestDbusFixture, NULL,
      test_dbus_setup, test_dbus_basic, test_dbus_teardown);

  return g_test_run ();
}
