#ifndef COMMON_H
#define COMMON_H

#include <gtk/gtk.h>
#include <glib.h>

/* Global flag for dry-run mode */
extern gboolean g_dry_run;

/* Status message helper */
void set_status(GtkLabel *status, const char *fmt, ...);

/* System info helpers */
gchar *get_os_name(void);
gchar *get_system_info_item(const char *cmd);
gchar *get_cpu_name(void);
gchar *get_ram_info(void);

/* Terminal prefix detection */
char *get_terminal_prefix(void);

/* Utility helpers */
gboolean user_exists(const char *username);
const char *find_sudo_group(void);
gboolean run_privileged_script(const char *script_contents, GtkLabel *status);
gboolean run_command_via_pkexec_stdin(const char *command, GtkLabel *status);
void run_command_and_report(const char *cmd, GtkLabel *status);
void open_config_dir(const char *subdir, GtkLabel *status);
void launch_if_found(const char *prog, GtkLabel *status);

/* Disk usage helper: returns a newly-allocated string like "26G/292G" for the
 * filesystem containing `path`. Caller must g_free() the returned string. */
gchar *get_disk_usage(const char *path);

/* Dialog windows */
GtkWindow *create_modal_window(GtkWindow *parent, const char *title);

#endif /* COMMON_H */
