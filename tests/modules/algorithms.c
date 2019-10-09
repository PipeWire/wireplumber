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

#include "../../modules/module-pipewire/algorithms.h"

static void
test_choose_sensible_raw_audio_format (void)
{
  uint32_t layout[] = { SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                        SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                        SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR };
  guint8 buffer[2048];
  struct spa_pod_builder b;
  struct spa_pod_frame f;
  struct spa_pod *param;
  struct spa_audio_info_raw info;
  g_autoptr (GPtrArray) formats = g_ptr_array_new ();

  /* test 1 */
  g_ptr_array_remove_range (formats, 0, formats->len);
  spa_pod_builder_init(&b, buffer, sizeof(buffer));
  spa_pod_builder_push_object (&b, &f, SPA_TYPE_OBJECT_Format, 0);
  spa_pod_builder_add (&b,
      SPA_FORMAT_mediaType,      SPA_POD_Id(SPA_MEDIA_TYPE_audio),
      SPA_FORMAT_mediaSubtype,   SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
      SPA_FORMAT_AUDIO_format,   SPA_POD_CHOICE_ENUM_Id(3,
                                    SPA_AUDIO_FORMAT_F32_OE,
                                    SPA_AUDIO_FORMAT_S16,
                                    SPA_AUDIO_FORMAT_S20),
      SPA_FORMAT_AUDIO_rate,     SPA_POD_CHOICE_RANGE_Int(22000, 44100, 8000),
      SPA_FORMAT_AUDIO_channels, SPA_POD_CHOICE_RANGE_Int(2, 1, 8),
      0);
  param = (struct spa_pod *) spa_pod_builder_pop (&b, &f);
  g_assert_nonnull (param);
  g_ptr_array_add (formats, param);

  g_assert_true (choose_sensible_raw_audio_format (formats, &info));
  g_assert_cmpint (info.format, ==, SPA_AUDIO_FORMAT_S16);
  g_assert_cmpint (info.rate, ==, 44100);
  g_assert_cmpint (info.channels, ==, 8);
  g_assert_cmpint (info.flags, ==, SPA_AUDIO_FLAG_UNPOSITIONED);


  /* test 2 */
  g_ptr_array_remove_range (formats, 0, formats->len);
  spa_pod_builder_init (&b, buffer, sizeof(buffer));
  spa_pod_builder_push_object (&b, &f, SPA_TYPE_OBJECT_Format, 0);
  spa_pod_builder_add (&b,
      SPA_FORMAT_mediaType,      SPA_POD_Id(SPA_MEDIA_TYPE_audio),
      SPA_FORMAT_mediaSubtype,   SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
      SPA_FORMAT_AUDIO_format,   SPA_POD_CHOICE_ENUM_Id(3,
                                    SPA_AUDIO_FORMAT_S32,
                                    SPA_AUDIO_FORMAT_U8,
                                    SPA_AUDIO_FORMAT_F32),
      SPA_FORMAT_AUDIO_rate,     SPA_POD_CHOICE_RANGE_Int(56000, 44100, 96000),
      SPA_FORMAT_AUDIO_channels, SPA_POD_Int(2),
      SPA_FORMAT_AUDIO_position, SPA_POD_Array(sizeof(uint32_t), SPA_TYPE_Id, 2, layout),
      0);
  param = (struct spa_pod *) spa_pod_builder_pop (&b, &f);
  g_assert_nonnull (param);
  g_ptr_array_add (formats, param);

  spa_pod_builder_push_object (&b, &f, SPA_TYPE_OBJECT_Format, 0);
  spa_pod_builder_add (&b,
      SPA_FORMAT_mediaType,      SPA_POD_Id(SPA_MEDIA_TYPE_audio),
      SPA_FORMAT_mediaSubtype,   SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
      SPA_FORMAT_AUDIO_format,   SPA_POD_CHOICE_ENUM_Id(3,
                                    SPA_AUDIO_FORMAT_S32,
                                    SPA_AUDIO_FORMAT_U8,
                                    SPA_AUDIO_FORMAT_F32),
      SPA_FORMAT_AUDIO_rate,     SPA_POD_CHOICE_RANGE_Int(56000, 44100, 96000),
      SPA_FORMAT_AUDIO_channels, SPA_POD_Int(5),
      SPA_FORMAT_AUDIO_position, SPA_POD_Array(sizeof(uint32_t), SPA_TYPE_Id, 5, layout),
      0);
  param = (struct spa_pod *) spa_pod_builder_pop (&b, &f);
  g_assert_nonnull (param);
  g_ptr_array_add (formats, param);

  g_assert_true (choose_sensible_raw_audio_format (formats, &info));
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

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/modules/algorithms/choose_sensible_raw_audio_format",
      test_choose_sensible_raw_audio_format);

  return g_test_run ();
}
