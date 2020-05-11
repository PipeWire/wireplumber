/* WirePlumber
 *
 * Copyright © 2019 Collabora Ltd.
 *    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <wp/wp.h>
#include <pipewire/pipewire.h>
#include <spa/pod/builder.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/layout.h>
#include <spa/param/audio/format.h>
#include <spa/param/audio/format-utils.h>

#include "../../modules/module-si-adapter/algorithms.h"

static void
test_choose_sensible_raw_audio_format (void)
{
  wp_spa_type_init (TRUE);

  uint32_t layout[] = { SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                        SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                        SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR };
  struct spa_audio_info_raw info;
  g_autoptr (GPtrArray) formats =
      g_ptr_array_new_with_free_func ((GDestroyNotify) wp_spa_pod_unref);

  {
    g_ptr_array_remove_range (formats, 0, formats->len);
    g_autoptr (WpSpaPod) param1 = wp_spa_pod_new_object (
        "Format", "Format",
        "mediaType",    SPA_POD_Id(SPA_MEDIA_TYPE_audio),
        "mediaSubtype", SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        "format",       SPA_POD_CHOICE_ENUM_Id(3,
                            SPA_AUDIO_FORMAT_F32_OE,
                            SPA_AUDIO_FORMAT_S16,
                            SPA_AUDIO_FORMAT_S20),
        "rate",         SPA_POD_CHOICE_RANGE_Int(22000, 44100, 8000),
        "channels",     SPA_POD_CHOICE_RANGE_Int(2, 1, 8),
        NULL);
    g_assert_nonnull (param1);
    g_ptr_array_add (formats, g_steal_pointer (&param1));
    g_assert_true (choose_sensible_raw_audio_format (formats, 34, &info));
    g_assert_cmpint (info.format, ==, SPA_AUDIO_FORMAT_S16);
    g_assert_cmpint (info.rate, ==, 44100);
    g_assert_cmpint (info.channels, ==, 8);
    g_assert_cmpint (info.flags, ==, SPA_AUDIO_FLAG_UNPOSITIONED);
  }

  {
    g_ptr_array_remove_range (formats, 0, formats->len);
    g_autoptr (WpSpaPod) param1 = wp_spa_pod_new_object (
        "Format", "Format",
        "mediaType",    SPA_POD_Id(SPA_MEDIA_TYPE_audio),
        "mediaSubtype", SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        "format",       SPA_POD_CHOICE_ENUM_Id(3,
                            SPA_AUDIO_FORMAT_F32_OE,
                            SPA_AUDIO_FORMAT_S16,
                            SPA_AUDIO_FORMAT_S20),
        "rate",         SPA_POD_CHOICE_RANGE_Int(22000, 44100, 8000),
        "channels",     SPA_POD_CHOICE_RANGE_Int(2, 1, 8),
        NULL);
    g_assert_nonnull (param1);
    g_ptr_array_add (formats, g_steal_pointer (&param1));
    g_assert_true (choose_sensible_raw_audio_format (formats, 2, &info));
    g_assert_cmpint (info.format, ==, SPA_AUDIO_FORMAT_S16);
    g_assert_cmpint (info.rate, ==, 44100);
    g_assert_cmpint (info.channels, ==, 2);
    g_assert_cmpint (info.flags, ==, SPA_AUDIO_FLAG_UNPOSITIONED);
  }

  {
    g_ptr_array_remove_range (formats, 0, formats->len);
    g_autoptr (WpSpaPod) param2 = wp_spa_pod_new_object (
        "Format", "Format",
        "mediaType",    SPA_POD_Id(SPA_MEDIA_TYPE_audio),
        "mediaSubtype", SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        "format",       SPA_POD_CHOICE_ENUM_Id(3,
                            SPA_AUDIO_FORMAT_S32,
                            SPA_AUDIO_FORMAT_U8,
                            SPA_AUDIO_FORMAT_F32),
        "rate",         SPA_POD_CHOICE_RANGE_Int(56000, 44100, 96000),
        "channels",     SPA_POD_Int(2),
        "position",     SPA_POD_Array(sizeof(uint32_t), SPA_TYPE_Id, 2, layout),
        NULL);
    g_assert_nonnull (param2);
    g_ptr_array_add (formats, g_steal_pointer (&param2));
    g_autoptr (WpSpaPod) param3 = wp_spa_pod_new_object (
        "Format", "Format",
        "mediaType",    SPA_POD_Id(SPA_MEDIA_TYPE_audio),
        "mediaSubtype", SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        "format",       SPA_POD_CHOICE_ENUM_Id(3,
                            SPA_AUDIO_FORMAT_S32,
                            SPA_AUDIO_FORMAT_U8,
                            SPA_AUDIO_FORMAT_F32),
        "rate",         SPA_POD_CHOICE_RANGE_Int(56000, 44100, 96000),
        "channels",     SPA_POD_Int(5),
        "position",     SPA_POD_Array(sizeof(uint32_t), SPA_TYPE_Id, 5, layout),
        NULL);
    g_assert_nonnull (param3);
    g_ptr_array_add (formats, g_steal_pointer (&param3));
    g_assert_true (choose_sensible_raw_audio_format (formats, 34, &info));
    g_assert_cmpint (info.format, ==, SPA_AUDIO_FORMAT_F32);
    g_assert_cmpint (info.rate, ==, 48000);
    g_assert_cmpint (info.channels, ==, 5);
    g_assert_cmpint (info.flags, ==, SPA_AUDIO_FLAG_NONE);
    g_assert_cmpint (info.position[0], ==, layout[0]);
    g_assert_cmpint (info.position[1], ==, layout[1]);
    g_assert_cmpint (info.position[2], ==, layout[2]);
    g_assert_cmpint (info.position[3], ==, layout[3]);
    g_assert_cmpint (info.position[4], ==, layout[4]);
    g_assert_cmpint (info.position[5], ==, 0);
  }

  wp_spa_type_deinit ();
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_log_set_writer_func (wp_log_writer_default, NULL, NULL);

  g_test_add_func ("/modules/algorithms/choose_sensible_raw_audio_format",
      test_choose_sensible_raw_audio_format);

  return g_test_run ();
}
