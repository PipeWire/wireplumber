/* PipeWire */
/* SPDX-FileCopyrightText: Copyright © 2018 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#ifndef FLATPAK_UTILS_H
#define FLATPAK_UTILS_H

#define HAVE_GLIB2

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif

#ifdef HAVE_GLIB2
#include <glib.h>
#endif

#include <spa/utils/cleanup.h>
#include <spa/utils/result.h>
#include <pipewire/log.h>

static int pw_check_flatpak_parse_metadata(const char *buf, size_t size, char **app_id, char **instance_id, char **devices)
{
#ifdef HAVE_GLIB2
	/*
	 * See flatpak-metadata(5)
	 *
	 * The .flatpak-info file is in GLib key_file .ini format.
	 */
	g_autoptr(GKeyFile) metadata = NULL;
	char *s;

	metadata = g_key_file_new();
	if (!g_key_file_load_from_data(metadata, buf, size, G_KEY_FILE_NONE, NULL))
		return -EINVAL;

	if (app_id) {
		s = g_key_file_get_value(metadata, "Application", "name", NULL);
		*app_id = s ? strdup(s) : NULL;
		g_free(s);
	}

	if (devices) {
		s = g_key_file_get_value(metadata, "Context", "devices", NULL);
		*devices = s ? strdup(s) : NULL;
		g_free(s);
	}

	if (instance_id) {
		s = g_key_file_get_value(metadata, "Instance", "instance-id", NULL);
		*instance_id = s ? strdup(s) : NULL;
		g_free(s);
	}

	return 0;
#else
	return -ENOTSUP;
#endif
}

static int pw_check_flatpak(pid_t pid, char **app_id, char **instance_id, char **devices)
{
#if defined(__linux__)
	char root_path[2048];
	struct stat stat_buf;
	int res;

	if (app_id)
		*app_id = NULL;
	if (instance_id)
		*instance_id = NULL;
	if (devices)
		*devices = NULL;

	snprintf(root_path, sizeof(root_path), "/proc/%d/root", (int)pid);

	spa_autoclose int root_fd = openat(AT_FDCWD, root_path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY);
	if (root_fd < 0) {
		res = -errno;
		pw_log_info("failed to open \"%s\": %s", root_path, spa_strerror(res));

		if (res == -EACCES) {
			/* If we can't access the root filesystem, consider not sandboxed.
			 * This should not happen but for now it is a workaround for selinux
			 * where we can't access the gnome-shell root when it connects for
			 * screen sharing.
			 */
			return 0;
		}

		/* Not able to open the root dir shouldn't happen. Probably the app died and
		 * we're failing due to /proc/$pid not existing. In that case fail instead
		 * of treating this as privileged. */
		return res;
	}

	spa_autoclose int info_fd = openat(root_fd, ".flatpak-info", O_RDONLY | O_CLOEXEC | O_NOCTTY);
	if (info_fd < 0) {
		if (errno == ENOENT) {
			pw_log_debug("no .flatpak-info, client on the host");
			/* No file => on the host */
			return 0;
		}
		res = -errno;
		pw_log_error("error opening .flatpak-info: %m");
		return res;
        }
	if (fstat (info_fd, &stat_buf) != 0 || !S_ISREG (stat_buf.st_mode)) {
		/* Some weird fd => failure, assume sandboxed */
		pw_log_error("error fstat .flatpak-info: %m");
	} else if (app_id || instance_id || devices) {
		/* Parse the application ID if needed */
		const size_t size = stat_buf.st_size;

		if (size > 0) {
			void *buf = mmap(NULL, size, PROT_READ, MAP_PRIVATE, info_fd, 0);
			if (buf != MAP_FAILED) {
				res = pw_check_flatpak_parse_metadata(buf, size, app_id, instance_id, devices);
				munmap(buf, size);
			} else {
				res = -errno;
			}
		} else {
			res = -EINVAL;
		}

		if (res == -EINVAL)
			pw_log_error("PID %d .flatpak-info file is malformed",
					(int)pid);
		else if (res < 0)
			pw_log_error("PID %d .flatpak-info parsing failed: %s",
					(int)pid, spa_strerror(res));
	}

	return 1;
#else
	return 0;
#endif
}

#endif /* FLATPAK_UTILS_H */
