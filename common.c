#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

/* global flag: if true, commands are dry-run only */
gboolean g_dry_run = FALSE;

/* Exported helper used by other modules */
void set_status(GtkLabel *status, const char *fmt, ...)
{
    va_list ap;
    char *msg;
    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    gtk_label_set_text(status, msg);
    g_free(msg);
}

/* System info helpers */
gchar *get_os_name(void)
{
    /* Try to read /etc/os-release or /etc/lsb-release */
    gchar *content = NULL;
    gchar **lines = NULL;
    gchar *result = NULL;

    if (g_file_get_contents("/etc/os-release", &content, NULL, NULL)) {
        lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "PRETTY_NAME=")) {
                gchar *value = g_strdup(lines[i] + 12); /* skip "PRETTY_NAME=" */
                g_strstrip(value);
                /* Remove surrounding quotes if present */
                if (value[0] == '"' && value[strlen(value)-1] == '"') {
                    gchar *unquoted = g_strndup(value + 1, strlen(value) - 2);
                    g_free(value);
                    value = unquoted;
                }
                result = value;
                break;
            }
        }
        g_strfreev(lines);
        g_free(content);
    }

    return result ? result : g_strdup("Linux");
}

gchar *get_system_info_item(const char *cmd)
{
    gchar *out = NULL;
    gchar *err = NULL;
    gint exit_status = 0;
    GError *error = NULL;

    if (!g_spawn_command_line_sync(cmd, &out, &err, &exit_status, &error)) {
        if (error) {
            g_clear_error(&error);
        }
        g_free(out);
        g_free(err);
        return g_strdup("Unknown");
    }

    if (out) {
        g_strchomp(out);
        return out;
    }
    g_free(err);
    return g_strdup("Unknown");
}

gchar *get_cpu_name(void)
{
    gchar *content = NULL;
    gchar **lines = NULL;
    gchar *result = NULL;

    if (g_file_get_contents("/proc/cpuinfo", &content, NULL, NULL)) {
        lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "model name")) {
                /* Find the colon and get the value after it */
                gchar *colon = g_strstr_len(lines[i], -1, ":");
                if (colon) {
                    gchar *value = g_strdup(colon + 1);
                    g_strstrip(value);
                    result = value;
                    break;
                }
            }
        }
        g_strfreev(lines);
        g_free(content);
    }

    return result ? result : g_strdup("Unknown CPU");
}

gchar *get_ram_info(void)
{
    gchar *content = NULL;
    gchar **lines = NULL;
    gchar *result = NULL;
    gulong total_kb = 0;

    if (g_file_get_contents("/proc/meminfo", &content, NULL, NULL)) {
        lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "MemTotal:")) {
                gchar *colon = g_strstr_len(lines[i], -1, ":");
                if (colon) {
                    gchar *value = g_strdup(colon + 1);
                    g_strstrip(value);
                    total_kb = strtoul(value, NULL, 10);
                    g_free(value);
                    break;
                }
            }
        }
        g_strfreev(lines);
        g_free(content);
    }

    if (total_kb > 0) {
        gdouble total_gb = (gdouble)total_kb / (1024 * 1024);
        result = g_strdup_printf("%.1f GB", total_gb);
    } else {
        result = g_strdup("Unknown");
    }

    return result;
}

gchar *get_disk_usage(const char *path)
{
    struct statvfs st;
    if (!path) path = "/";
    if (statvfs(path, &st) != 0) {
        return g_strdup("Unknown");
    }

    unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
    unsigned long long freeb = (unsigned long long)st.f_bfree * st.f_frsize;
    unsigned long long used = 0;
    if (total > freeb) used = total - freeb;

    double total_g = (double)total / (1024.0 * 1024.0 * 1024.0);
    double used_g = (double)used / (1024.0 * 1024.0 * 1024.0);

    /* Choose units: GB if >=1, else MB */
    if (total_g >= 1.0) {
        return g_strdup_printf("%.1fG/%.1fG", used_g, total_g);
    } else {
        double total_m = (double)total / (1024.0 * 1024.0);
        double used_m = (double)used / (1024.0 * 1024.0);
        return g_strdup_printf("%.0fM/%.0fM", used_m, total_m);
    }
}

/* Terminal prefix detection */
char *get_terminal_prefix(void)
{
    const char *candidates[] = {
        "kitty",
        "xdg-terminal",
        "x-terminal-emulator",
        "alacritty",
        "konsole",
        "gnome-terminal",
        "xfce4-terminal",
        "xterm",
        NULL
    };

    for (int i = 0; candidates[i] != NULL; i++) {
        char *path = g_find_program_in_path(candidates[i]);
        if (path) {
            char *prefix = NULL;
            if (g_strcmp0(candidates[i], "gnome-terminal") == 0) {
                prefix = g_strdup_printf("%s -- bash -lc '%%s'", candidates[i]);
            } else {
                prefix = g_strdup_printf("%s -e bash -lc '%%s'", candidates[i]);
            }
            g_free(path);
            return prefix;
        }
    }

    return g_strdup("xterm -e bash -lc '%s'");
}

/* Utilities */
gboolean user_exists(const char *username)
{
    struct passwd *pw = getpwnam(username);
    return pw != NULL;
}

const char *find_sudo_group(void)
{
    struct group *g;
    g = getgrnam("sudo");
    if (g) return "sudo";
    g = getgrnam("wheel");
    if (g) return "wheel";
    return NULL;
}

void run_command_and_report(const char *cmd, GtkLabel *status)
{
    GError *error = NULL;
    if (!g_spawn_command_line_async(cmd, &error)) {
        set_status(status, "Failed to start: %s", error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
        set_status(status, "Started: %s", cmd);
    }
}

/* Launch a program (found in PATH) and report status */
void launch_if_found(const char *prog, GtkLabel *status)
{
    char *path = g_find_program_in_path(prog);
    if (path) {
        run_command_and_report(path, status);
        g_free(path);
    } else {
        set_status(status, "%s not found in PATH", prog);
    }
}

/* Helper: open a config directory under ~/.config with xdg-open */
void open_config_dir(const char *subdir, GtkLabel *status)
{
    char *path = g_build_filename(g_get_home_dir(), ".config", subdir, NULL);
    char *fm = g_find_program_in_path("thunar");
    if (fm) {
        char *q = g_shell_quote(path);
        char *cmd = g_strdup_printf("%s %s", fm, q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
        g_free(fm);
        g_free(path);
        return;
    }

    char *q = g_shell_quote(path);
    char *cmd = g_strdup_printf("xdg-open %s", q);
    run_command_and_report(cmd, status);
    g_free(path);
    g_free(q);
    g_free(cmd);
}

/* Dialog windows */
GtkWindow *create_modal_window(GtkWindow *parent, const char *title)
{
    GtkWindow *w = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(w, title);
    gtk_window_set_transient_for(w, parent);
    gtk_window_set_modal(w, TRUE);
    gtk_window_set_default_size(w, 400, 200);
    return w;
}

/* Child-watch data for processes */
typedef struct {
    GtkLabel *status;
} ChildWatchData;

static void on_child_exit(GPid pid, gint status_code, gpointer user_data)
{
    ChildWatchData *cd = (ChildWatchData *)user_data;
    g_spawn_close_pid(pid);

    if (WIFEXITED(status_code)) {
        int es = WEXITSTATUS(status_code);
        if (es == 0) {
            set_status(cd->status, "Command completed successfully");
        } else {
            set_status(cd->status, "Command failed (exit %d)", es);
        }
    } else if (WIFSIGNALED(status_code)) {
        set_status(cd->status, "Command killed by signal %d", WTERMSIG(status_code));
    } else {
        set_status(cd->status, "Command ended (status %d)", status_code);
    }

    g_free(cd);
}

gboolean run_privileged_script(const char *script_contents, GtkLabel *status)
{
    return run_command_via_pkexec_stdin(script_contents, status);
}

#ifdef DEBUG_ENABLE
void debug_log(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    char *msg = NULL;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t sec = ts.tv_sec;
    struct tm tm;
    localtime_r(&sec, &tm);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    fprintf(stderr, "[%s.%03ld] DEBUG %s:%d: %s\n", timebuf, ts.tv_nsec / 1000000, file, line, msg);
    fflush(stderr);
    g_free(msg);
}
#endif

gboolean run_command_via_pkexec_stdin(const char *command, GtkLabel *status)
{
    GError *error = NULL;
    char *pkexec_path = g_find_program_in_path("pkexec");
    if (!pkexec_path) {
        set_status(status, "pkexec not found; cannot run as root without terminal");
        return FALSE;
    }

    gchar *argv[] = { pkexec_path, "/bin/bash", "-s", NULL };
    GPid child_pid;
    gint stdin_fd = -1;

    if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &child_pid, &stdin_fd, NULL, NULL, &error)) {
        set_status(status, "Failed to spawn pkexec: %s", error ? error->message : "unknown");
        g_clear_error(&error);
        g_free(pkexec_path);
        return FALSE;
    }

    write(stdin_fd, command, strlen(command));
    write(stdin_fd, "\n", 1);
    close(stdin_fd);

    ChildWatchData *cd = g_new0(ChildWatchData, 1);
    cd->status = status;
    g_child_watch_add(child_pid, on_child_exit, cd);

    set_status(status, "Launched privileged command via pkexec; authentication may be requested.");

    g_free(pkexec_path);
    return TRUE;
}
