/* PipeWire */
/* SPDX-FileCopyrightText: Copyright © 2021 Wim Taymans */
/* SPDX-License-Identifier: MIT */

/*
  This is a partial copy of functions from libpipewire's conf.c that is meant to
  live here temporarily until pw_context_parse_conf_section() is fixed upstream.
  See https://gitlab.freedesktop.org/pipewire/pipewire/-/merge_requests/1925
*/

#include <string.h>

#include <spa/utils/result.h>
#include <spa/utils/string.h>
#include <spa/utils/json.h>
#include <spa/utils/cleanup.h>

#include <pipewire/impl.h>

struct data {
	struct pw_context *context;
	struct pw_properties *props;
	int count;
};

/* context.spa-libs = {
 *  <factory-name regex> = <library-name>
 * }
 */
static int parse_spa_libs(void *user_data, const char *location,
		const char *section, const char *str, size_t len)
{
	struct data *d = user_data;
	struct pw_context *context = d->context;
	struct spa_json it[2];
	char key[512], value[512];

	spa_json_init(&it[0], str, len);
	if (spa_json_enter_object(&it[0], &it[1]) < 0) {
		pw_log_error("config file error: context.spa-libs is not an object");
		return -EINVAL;
	}

	while (spa_json_get_string(&it[1], key, sizeof(key)) > 0) {
		if (spa_json_get_string(&it[1], value, sizeof(value)) > 0) {
			pw_context_add_spa_lib(context, key, value);
			d->count++;
		}
	}
	return 0;
}



static int load_module(struct pw_context *context, const char *key, const char *args, const char *flags)
{
	if (pw_context_load_module(context, key, args, NULL) == NULL) {
		if (errno == ENOENT && flags && strstr(flags, "ifexists") != NULL) {
			pw_log_info("%p: skipping unavailable module %s",
					context, key);
		} else if (flags == NULL || strstr(flags, "nofail") == NULL) {
			pw_log_error("%p: could not load mandatory module \"%s\": %m",
					context, key);
			return -errno;
		} else {
			pw_log_info("%p: could not load optional module \"%s\": %m",
					context, key);
		}
	} else {
		pw_log_info("%p: loaded module %s", context, key);
	}
	return 0;
}

/*
 * context.modules = [
 *   {   name = <module-name>
 *       ( args = { <key> = <value> ... } )
 *       ( flags = [ ( ifexists ) ( nofail ) ]
 *       ( condition = [ { key = value, .. } .. ] )
 *   }
 * ]
 */
static int parse_modules(void *user_data, const char *location,
		const char *section, const char *str, size_t len)
{
	struct data *d = user_data;
	struct pw_context *context = d->context;
	struct spa_json it[4];
	char key[512];
	int res = 0;

	spa_autofree char *s = strndup(str, len);
	spa_json_init(&it[0], s, len);
	if (spa_json_enter_array(&it[0], &it[1]) < 0) {
		pw_log_error("config file error: context.modules is not an array");
		return -EINVAL;
	}

	while (spa_json_enter_object(&it[1], &it[2]) > 0) {
		char *name = NULL, *args = NULL, *flags = NULL;
		bool have_match = true;

		while (spa_json_get_string(&it[2], key, sizeof(key)) > 0) {
			const char *val;
			int len;

			if ((len = spa_json_next(&it[2], &val)) <= 0)
				break;

			if (spa_streq(key, "name")) {
				name = (char*)val;
				spa_json_parse_stringn(val, len, name, len+1);
			} else if (spa_streq(key, "args")) {
				if (spa_json_is_container(val, len))
					len = spa_json_container_len(&it[2], val, len);

				args = (char*)val;
				spa_json_parse_stringn(val, len, args, len+1);
			} else if (spa_streq(key, "flags")) {
				if (spa_json_is_container(val, len))
					len = spa_json_container_len(&it[2], val, len);
				flags = (char*)val;
				spa_json_parse_stringn(val, len, flags, len+1);
			}
		}
		if (!have_match)
			continue;

		if (name != NULL)
			res = load_module(context, name, args, flags);

		if (res < 0)
			break;

		d->count++;
	}

	return res;
}

static int _pw_context_parse_conf_section(struct pw_context *context,
		struct pw_properties *conf, const char *section)
{
	struct data data = { .context = context };
	int res;

	if (spa_streq(section, "context.spa-libs"))
		res = pw_conf_section_for_each(&conf->dict, section,
				parse_spa_libs, &data);
	else if (spa_streq(section, "context.modules"))
		res = pw_conf_section_for_each(&conf->dict, section,
				parse_modules, &data);
	else
		res = -EINVAL;

	return res == 0 ? data.count : res;
}
