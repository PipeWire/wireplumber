/* WirePlumber
 *
 * Copyright © 2024 Collabora Ltd.
 *    @author Julian Bouzas <julian.bouzas@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdio.h>
#include <spa/utils/cleanup.h>

#include "log.h"
#include "proc-utils.h"

#define MAX_ARGS 1024

WP_DEFINE_LOCAL_LOG_TOPIC ("wp-proc-utils")

/*! \defgroup wpprocutils Process Utilities */

/*!
 * \struct WpProcInfo
 *
 * WpProcInfo holds information of a process.
 */
struct _WpProcInfo {
  grefcount ref;
  pid_t pid;
  pid_t parent;
  gchar *cgroup;
  gchar *args[MAX_ARGS];
  guint n_args;
};

G_DEFINE_BOXED_TYPE (WpProcInfo, wp_proc_info, wp_proc_info_ref,
    wp_proc_info_unref)

/*!
 * \brief Increases the reference count of a process information object
 * \ingroup wpprocutils
 * \param self a process information object
 * \returns (transfer full): \a self with an additional reference count on it
 */
WpProcInfo *
wp_proc_info_ref (WpProcInfo * self)
{
  g_ref_count_inc (&self->ref);
  return self;
}

static void
wp_proc_info_free (WpProcInfo * self)
{
  g_clear_pointer (&self->cgroup, g_free);
  for (guint i = 0; i < MAX_ARGS; i++)
    g_clear_pointer (&self->args[i], free);
  g_slice_free (WpProcInfo, self);
}

/*!
 * \brief Decreases the reference count on \a self and frees it when the ref
 * count reaches zero.
 * \ingroup wpprocutils
 * \param self (transfer full): a process information object
 */
void
wp_proc_info_unref (WpProcInfo * self)
{
  if (g_ref_count_dec (&self->ref))
    wp_proc_info_free (self);
}

static WpProcInfo *
wp_proc_info_new (pid_t pid)
{
  WpProcInfo *self = g_slice_new0 (WpProcInfo);
  g_ref_count_init (&self->ref);
  self->pid = pid;
  self->parent = 0;
  self->cgroup = NULL;
  for (guint i = 0; i < MAX_ARGS; i++)
    self->args[i] = NULL;
  return self;
}

/*!
 * \brief Gets the PID of a process information object
 * \ingroup wpprocutils
 * \param self the process information object
 * \returns the PID of the process information object
 */
pid_t
wp_proc_info_get_pid (WpProcInfo * self)
{
  return self->pid;
}

/*!
 * \brief Gets the parent PID of a process information object
 * \ingroup wpprocutils
 * \param self the process information object
 * \returns the parent PID of the process information object
 */
pid_t
wp_proc_info_get_parent_pid (WpProcInfo * self)
{
  return self->parent;
}

/*!
 * \brief Gets the number of args of a process information object
 * \ingroup wpprocutils
 * \param self the process information object
 * \returns the number of args of the process information object
 */
guint
wp_proc_info_get_n_args (WpProcInfo * self)
{
  return self->n_args;
}

/*!
 * \brief Gets the indexed arg of a process information object
 * \ingroup wpprocutils
 * \param self the process information object
 * \param index the index of the arg
 * \returns the indexed arg of the process information object
 */
const gchar *
wp_proc_info_get_arg (WpProcInfo * self, guint index)
{
  if (index >= self->n_args)
    return NULL;
  return self->args[index];
}

/*!
 * \brief Gets the systemd cgroup of a process information object
 * \ingroup wpprocutils
 * \param self the process information object
 * \returns the systemd cgroup of the process information object
 */
const gchar *
wp_proc_info_get_cgroup (WpProcInfo * self)
{
  return self->cgroup;
}

static FILE *
fdopenat (int dirfd, const char *path, int flags, const char *mode, mode_t perm)
{
  int fd = openat (dirfd, path, flags, perm);
  if (fd >= 0) {
    FILE *f = fdopen (fd, mode);
    if (f)
      return f;
    close (fd);
  }

  return NULL;
}


/*!
 * \brief Gets the process information of a given PID
 * \ingroup wpprocutils
 * \param pid the PID to get the process information from
 * \returns: (transfer full): the process information of the given PID
 */
WpProcInfo *
wp_proc_utils_get_proc_info (pid_t pid)
{
  WpProcInfo *ret = wp_proc_info_new (pid);
  char path [64];
  spa_autoclose int base_fd = -1;
  FILE *file;
  g_autofree gchar *line = NULL;
  size_t size = 0;

  snprintf (path, sizeof(path), "/proc/%d", pid);
  base_fd = open (path,
      O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY, 0);
  if (base_fd < 0) {
    wp_info ("Could not open process info directory %s, skipping", path);
    return ret;
  }

  /* Get parent PID */
  file = fdopenat (base_fd, "status",
      O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY, "r", 0);
  if (file) {
    while (getline (&line, &size, file) > 1)
      if (sscanf (line, "PPid:%d\n", &ret->parent) == 1)
        break;
    fclose (file);
  }

  /* Get cgroup */
  file = fdopenat (base_fd, "cgroup",
      O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY, "r", 0);
  if (file) {
    if (getline (&line, &size, file) > 1)
      ret->cgroup = g_strstrip (g_strdup (line));
    fclose (file);
  }

  /* Get args */
  file = fdopenat (base_fd, "cmdline",
      O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY, "r", 0);
  if (file) {
    while (getdelim (&line, &size, 0, file) > 1 && ret->n_args < MAX_ARGS)
      ret->args[ret->n_args++] = g_strdup (line);
    fclose (file);
  }

  return ret;
}
