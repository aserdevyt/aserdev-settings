/*
 * AserDev Settings - GTK4 (minimal)
 *
 * - Left sidebar: Packages | Users (placeholder) | Wallpaper
 * - Packages page: entry for package + buttons to run AUR/Pacman/Flatpak/update commands
 * - Wallpaper page: runs `waypaper`
 *
 * Commands are launched with g_spawn_command_line_async and status updates
 * appear in the status label (no blocking modal dialogs).
 */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

typedef struct {
    GtkWindow  *parent;
    GtkEntry   *entry;
    GtkLabel   *status_label;
} PackageActionData;

/* global flag: if true, commands are dry-run only */
static gboolean g_dry_run = FALSE;

/* Exported helper used by other modules (hypr module) */
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

#include "hypr.h"

static void run_command_and_report(const char *cmd, GtkLabel *status)
{
    GError *error = NULL;
    if (!g_spawn_command_line_async(cmd, &error)) {
        set_status(status, "Failed to start: %s", error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
        set_status(status, "Started: %s", cmd);
    }
}

/* forward-declare helper used by run-command page */
static gboolean run_command_via_pkexec_stdin(const char *command, GtkLabel *status);

/*
 * Detect a terminal emulator to use. Prefer kitty, then try a small set of
 * fallbacks. Return a newly-allocated prefix string that contains a single
 * "%s" where the inner shell command should be placed. Examples:
 *  "kitty -e bash -lc '%s'"
 *  "gnome-terminal -- bash -lc '%s'"
 */
static char *get_terminal_prefix(void)
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

    /* conservative default: use xterm if nothing else found */
    return g_strdup("xterm -e bash -lc '%s'");
}

static void on_package_action(GtkButton *button, gpointer user_data)
{
    PackageActionData *d = (PackageActionData *)user_data;
    const char *action = (const char *)g_object_get_data(G_OBJECT(button), "action");
    const char *pkg = gtk_editable_get_text(GTK_EDITABLE(d->entry));

    if (!pkg || strlen(pkg) == 0) {
        set_status(d->status_label, "Please enter a package name.");
        return;
    }

    gboolean execute = !g_dry_run; /* execute unless running in --dry-run mode */
    char *cmd = NULL;

    if (g_strcmp0(action, "aur") == 0) {
        /* use yay with --needed and --noconfirm, open in detected terminal */
        char *prefix = get_terminal_prefix();
        char *quoted = g_shell_quote(pkg);
        cmd = g_strdup_printf(prefix, g_strdup_printf("yay -S --needed --noconfirm %s", quoted));
        g_free(prefix);
        g_free(quoted);
    } else if (g_strcmp0(action, "pacman") == 0) {
        /* pacman via sudo with --noconfirm, open in detected terminal */
        char *prefix = get_terminal_prefix();
        char *quoted = g_shell_quote(pkg);
        cmd = g_strdup_printf(prefix, g_strdup_printf("sudo pacman -S --noconfirm %s", quoted));
        g_free(prefix);
        g_free(quoted);
    } else if (g_strcmp0(action, "flatpak") == 0) {
        /* open flatpak install in detected terminal */
        char *prefix = get_terminal_prefix();
        char *quoted = g_shell_quote(pkg);
        cmd = g_strdup_printf(prefix, g_strdup_printf("flatpak install -y flathub %s", quoted));
        g_free(prefix);
        g_free(quoted);
    } else if (g_strcmp0(action, "update_aur_flatpak") == 0) {
        /* update both with yay and flatpak inside detected terminal */
        char *prefix = get_terminal_prefix();
        cmd = g_strdup_printf(prefix, "yay -Syu && flatpak update -y");
        g_free(prefix);
    } else {
        cmd = g_strdup("echo unknown-action");
    }

    if (!execute) {
        set_status(d->status_label, "Dry run: %s", cmd);
        g_free(cmd);
        return;
    }

    run_command_and_report(cmd, d->status_label);
    g_free(cmd);
}

static void on_run_waypaper(GtkButton *button, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    const char *cmd = "waypaper";
    run_command_and_report(cmd, status);
}

/* Helper: open a config directory under ~/.config with xdg-open */
static void open_config_dir(const char *subdir, GtkLabel *status)
{
    char *path = g_build_filename(g_get_home_dir(), ".config", subdir, NULL);
    /* prefer Thunar if available (opens folders reliably), otherwise fall back to xdg-open */
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

/* Launch a program (found in PATH) and report status */
static void launch_if_found(const char *prog, GtkLabel *status)
{
    char *path = g_find_program_in_path(prog);
    if (path) {
        run_command_and_report(path, status);
        g_free(path);
    } else {
        set_status(status, "%s not found in PATH", prog);
    }
}

/* Clipboard page: open rofi-cliphist or wipe via cliphist */
static void on_clipboard_open(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    char *path = g_find_program_in_path("rofi-cliphist");
    if (path) {
        run_command_and_report(path, status);
        g_free(path);
    } else {
        set_status(status, "rofi-cliphist not found in PATH");
    }
}

static void on_clipboard_wipe(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    char *path = g_find_program_in_path("cliphist");
    if (path) {
        char *cmd = g_strdup_printf("%s wipe", path);
        run_command_and_report(cmd, status);
        g_free(cmd);
        g_free(path);
    } else {
        set_status(status, "cliphist not found in PATH");
    }
}

static GtkWidget *create_clipboard_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Clipboard tools");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *btn_open = gtk_button_new_with_label("Open clipboard (rofi-cliphist)");
    g_signal_connect(btn_open, "clicked", G_CALLBACK(on_clipboard_open), status_label);
    gtk_box_append(GTK_BOX(vbox), btn_open);

    GtkWidget *btn_wipe = gtk_button_new_with_label("Empty clipboard (cliphist wipe)");
    g_signal_connect(btn_wipe, "clicked", G_CALLBACK(on_clipboard_wipe), status_label);
    gtk_box_append(GTK_BOX(vbox), btn_wipe);

    return vbox;
}

/* Screen recording */
static void on_screenrec_run(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    char *path = g_find_program_in_path("screenrec");
    if (path) {
        run_command_and_report(path, status);
        g_free(path);
    } else {
        set_status(status, "screenrec not found in PATH");
    }
}

static GtkWidget *create_screenrec_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Screen recording");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *btn = gtk_button_new_with_label("Run screenrec");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_screenrec_run), status_label);
    gtk_box_append(GTK_BOX(vbox), btn);

    return vbox;
}

/* Emoji and Run App (rofi) */
static void on_rofi_emoji(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    run_command_and_report("rofi -show emoji", status);
}

static void on_rofi_drun(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    run_command_and_report("rofi -show drun", status);
}

static GtkWidget *create_emoji_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Emoji picker (rofi)");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *btn = gtk_button_new_with_label("Open Emoji (rofi)");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_rofi_emoji), status_label);
    gtk_box_append(GTK_BOX(vbox), btn);

    return vbox;
}

static GtkWidget *create_runapp_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Run application (rofi drun)");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *btn = gtk_button_new_with_label("Run app (rofi -show drun)");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_rofi_drun), status_label);
    gtk_box_append(GTK_BOX(vbox), btn);

    return vbox;
}

/* Run Command page: ask for command and optionally run as root inside a terminal */
static void on_run_command_clicked(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkEntry *entry = GTK_ENTRY(ud[0]);
    GtkCheckButton *chk_root = GTK_CHECK_BUTTON(ud[1]);
    GtkCheckButton *chk_terminal = GTK_CHECK_BUTTON(ud[2]);
    GtkLabel *status = GTK_LABEL(ud[3]);

    const char *cmdtext = gtk_editable_get_text(GTK_EDITABLE(entry));
    gboolean as_root = gtk_check_button_get_active(chk_root);
    gboolean run_in_terminal = gtk_check_button_get_active(chk_terminal);

    if (!cmdtext || strlen(cmdtext) == 0) {
        set_status(status, "Please enter a command to run");
        return;
    }

    /* If user chose to run inside a terminal, open the preferred terminal
     * emulator and run via bash -lc (optionally prefixed with sudo). */
    if (run_in_terminal) {
        char *quoted = g_shell_quote(cmdtext);
        char *inner = NULL;
        if (as_root) {
            inner = g_strdup_printf("sudo bash -lc %s", quoted);
        } else {
            inner = g_strdup_printf("bash -lc %s", quoted);
        }
        char *prefix = get_terminal_prefix();
        char *cmd = g_strdup_printf(prefix, inner);
        run_command_and_report(cmd, status);
        g_free(quoted);
        g_free(inner);
        g_free(prefix);
        g_free(cmd);
        return;
    }

    /* Non-interactive mode: if running as root, use pkexec and pass the
     * command via stdin to /bin/bash -s; otherwise run bash -lc directly. */
    if (as_root) {
        run_command_via_pkexec_stdin(cmdtext, status);
    } else {
        /* run non-interactively via bash -lc */
        char *quoted = g_shell_quote(cmdtext);
        char *cmd = g_strdup_printf("bash -lc %s", quoted);
        run_command_and_report(cmd, status);
        g_free(quoted);
        g_free(cmd);
    }
}

static GtkWidget *create_runcommand_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Run command");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Command to run, e.g. top");
    gtk_box_append(GTK_BOX(vbox), entry);

    GtkWidget *chk_root = gtk_check_button_new_with_label("Run as root (sudo)");
    gtk_box_append(GTK_BOX(vbox), chk_root);

    GtkWidget *chk_terminal = gtk_check_button_new_with_label("Run in terminal");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_terminal), TRUE);
    gtk_box_append(GTK_BOX(vbox), chk_terminal);

    GtkWidget *btn = gtk_button_new_with_label("Run");
    gpointer *ud = g_new(gpointer, 4);
    ud[0] = entry;
    ud[1] = chk_root;
    ud[2] = chk_terminal;
    ud[3] = status_label;
    g_signal_connect(btn, "clicked", G_CALLBACK(on_run_command_clicked), ud);
    gtk_box_append(GTK_BOX(vbox), btn);

    return vbox;
}

/* Default apps page: let the user set desktop entries for common roles using xdg */
/* Helper: query xdg-mime for the current default of a given mime/handler.
 * Returns a newly-allocated string (caller must g_free) or NULL on failure. */
static char *query_xdg_mime_default(const char *mime)
{
    gchar *out = NULL;
    gchar *err = NULL;
    gint exit_status = 0;
    GError *error = NULL;
    char *cmd = g_strdup_printf("xdg-mime query default %s", mime);
    gboolean ok = g_spawn_command_line_sync(cmd, &out, &err, &exit_status, &error);
    g_free(cmd);

    if (!ok) {
        if (error) g_clear_error(&error);
        g_free(out);
        g_free(err);
        return NULL;
    }

    if (!out) {
        g_free(err);
        return NULL;
    }

    g_strchomp(out);
    char *res = g_strdup(out);
    g_free(out);
    g_free(err);
    return res;
}

static void on_default_apps_apply(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkEntry *entry_term = GTK_ENTRY(ud[0]);
    GtkEntry *entry_fm = GTK_ENTRY(ud[1]);
    GtkEntry *entry_browser = GTK_ENTRY(ud[2]);
    GtkEntry *entry_editor = GTK_ENTRY(ud[3]);
    GtkLabel *status = GTK_LABEL(ud[4]);
    GtkLabel *cur_term = GTK_LABEL(ud[5]);
    GtkLabel *cur_fm = GTK_LABEL(ud[6]);
    GtkLabel *cur_browser = GTK_LABEL(ud[7]);
    GtkLabel *cur_editor = GTK_LABEL(ud[8]);

    const char *term = gtk_editable_get_text(GTK_EDITABLE(entry_term));
    const char *fm = gtk_editable_get_text(GTK_EDITABLE(entry_fm));
    const char *browser = gtk_editable_get_text(GTK_EDITABLE(entry_browser));
    const char *editor = gtk_editable_get_text(GTK_EDITABLE(entry_editor));

    if (term && strlen(term) > 0) {
        char *q = g_shell_quote(term);
        char *cmd = g_strdup_printf("xdg-mime default %s x-scheme-handler/terminal", q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
    }
    if (fm && strlen(fm) > 0) {
        char *q = g_shell_quote(fm);
        char *cmd = g_strdup_printf("xdg-mime default %s inode/directory", q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
    }
    if (browser && strlen(browser) > 0) {
        char *q = g_shell_quote(browser);
        /* Use xdg-mime for HTTP handler so we get a desktop file name */
        char *cmd = g_strdup_printf("xdg-mime default %s x-scheme-handler/http", q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
    }
    if (editor && strlen(editor) > 0) {
        char *q = g_shell_quote(editor);
        char *cmd = g_strdup_printf("xdg-mime default %s text/plain", q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
    }

    set_status(status, "Default app commands launched (check status)");

    /* Refresh the visible current-default labels */
    if (cur_term) {
        char *val = NULL;
        val = g_strdup(query_xdg_mime_default("x-scheme-handler/terminal"));
        if (val) {
            gtk_label_set_text(cur_term, val);
            g_free(val);
        } else {
            gtk_label_set_text(cur_term, "(unknown)");
        }
    }
    if (cur_fm) {
        char *val = NULL;
        val = g_strdup(query_xdg_mime_default("inode/directory"));
        if (val) {
            gtk_label_set_text(cur_fm, val);
            g_free(val);
        } else {
            gtk_label_set_text(cur_fm, "(unknown)");
        }
    }
    if (cur_browser) {
        char *val = NULL;
        val = g_strdup(query_xdg_mime_default("x-scheme-handler/http"));
        if (val) {
            gtk_label_set_text(cur_browser, val);
            g_free(val);
        } else {
            gtk_label_set_text(cur_browser, "(unknown)");
        }
    }
    if (cur_editor) {
        char *val = NULL;
        val = g_strdup(query_xdg_mime_default("text/plain"));
        if (val) {
            gtk_label_set_text(cur_editor, val);
            g_free(val);
        } else {
            gtk_label_set_text(cur_editor, "(unknown)");
        }
    }
}

/* File-chooser helpers for selecting .desktop files and putting the basename
 * into a GtkEntry. */
static void on_defaultapp_filechosen(GtkNativeDialog *dialog, int response, gpointer user_data)
{
    GtkEntry *entry = GTK_ENTRY(user_data);
    if (response == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GFile *file = gtk_file_chooser_get_file(chooser);
        if (file) {
            char *path = g_file_get_path(file);
            if (path) {
                char *base = g_path_get_basename(path);
                gtk_editable_set_text(GTK_EDITABLE(entry), base);
                g_free(base);
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    gtk_native_dialog_destroy(dialog);
}

static void on_choose_desktop_clicked(GtkButton *btn, gpointer user_data)
{
    GtkEntry *entry = GTK_ENTRY(user_data);
    GtkFileChooserNative *fc = gtk_file_chooser_native_new("Choose .desktop file", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, "Select", "Cancel");
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(fc), FALSE);
    g_signal_connect(fc, "response", G_CALLBACK(on_defaultapp_filechosen), entry);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(fc));
}

static GtkWidget *create_defaultapps_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Default Applications");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Choose the .desktop file name that should be used as the default for each role. Use the Choose button to browse for a .desktop file. Current defaults are shown on the right.");
    gtk_widget_set_hexpand(desc, TRUE);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(vbox), grid);

    /* Terminal */
    GtkWidget *lbl_term = gtk_label_new("Terminal:");
    gtk_widget_set_halign(lbl_term, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl_term, 0, 0, 1, 1);
    GtkWidget *entry_term = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_term), "e.g. org.gnome.Terminal.desktop");
    gtk_widget_set_hexpand(entry_term, TRUE);
    gtk_widget_set_size_request(entry_term, 520, -1);
    gtk_grid_attach(GTK_GRID(grid), entry_term, 1, 0, 1, 1);
    GtkWidget *btn_term = gtk_button_new_with_label("Choose");
    gtk_grid_attach(GTK_GRID(grid), btn_term, 2, 0, 1, 1);
    GtkWidget *cur_term = gtk_label_new("(unknown)");
    gtk_widget_set_halign(cur_term, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), cur_term, 3, 0, 1, 1);

    /* File manager */
    GtkWidget *lbl_fm = gtk_label_new("File manager:");
    gtk_widget_set_halign(lbl_fm, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl_fm, 0, 1, 1, 1);
    GtkWidget *entry_fm = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_fm), "e.g. org.gnome.Nautilus.desktop");
    gtk_widget_set_hexpand(entry_fm, TRUE);
    gtk_widget_set_size_request(entry_fm, 520, -1);
    gtk_grid_attach(GTK_GRID(grid), entry_fm, 1, 1, 1, 1);
    GtkWidget *btn_fm = gtk_button_new_with_label("Choose");
    gtk_grid_attach(GTK_GRID(grid), btn_fm, 2, 1, 1, 1);
    GtkWidget *cur_fm = gtk_label_new("(unknown)");
    gtk_widget_set_halign(cur_fm, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), cur_fm, 3, 1, 1, 1);

    /* Browser */
    GtkWidget *lbl_browser = gtk_label_new("Web browser:");
    gtk_widget_set_halign(lbl_browser, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl_browser, 0, 2, 1, 1);
    GtkWidget *entry_browser = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_browser), "e.g. firefox.desktop");
    gtk_widget_set_hexpand(entry_browser, TRUE);
    gtk_widget_set_size_request(entry_browser, 520, -1);
    gtk_grid_attach(GTK_GRID(grid), entry_browser, 1, 2, 1, 1);
    GtkWidget *btn_browser = gtk_button_new_with_label("Choose");
    gtk_grid_attach(GTK_GRID(grid), btn_browser, 2, 2, 1, 1);
    GtkWidget *cur_browser = gtk_label_new("(unknown)");
    gtk_widget_set_halign(cur_browser, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), cur_browser, 3, 2, 1, 1);

    /* Text editor */
    GtkWidget *lbl_editor = gtk_label_new("Text editor:");
    gtk_widget_set_halign(lbl_editor, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl_editor, 0, 3, 1, 1);
    GtkWidget *entry_editor = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_editor), "e.g. org.gnome.gedit.desktop");
    gtk_widget_set_hexpand(entry_editor, TRUE);
    gtk_widget_set_size_request(entry_editor, 520, -1);
    gtk_grid_attach(GTK_GRID(grid), entry_editor, 1, 3, 1, 1);
    GtkWidget *btn_editor = gtk_button_new_with_label("Choose");
    gtk_grid_attach(GTK_GRID(grid), btn_editor, 2, 3, 1, 1);
    GtkWidget *cur_editor = gtk_label_new("(unknown)");
    gtk_widget_set_halign(cur_editor, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), cur_editor, 3, 3, 1, 1);

    GtkWidget *btn = gtk_button_new_with_label("Apply defaults");
    gpointer *ud = g_new(gpointer, 9);
    ud[0] = entry_term;
    ud[1] = entry_fm;
    ud[2] = entry_browser;
    ud[3] = entry_editor;
    ud[4] = status_label;
    ud[5] = cur_term;
    ud[6] = cur_fm;
    ud[7] = cur_browser;
    ud[8] = cur_editor;
    g_signal_connect(btn, "clicked", G_CALLBACK(on_default_apps_apply), ud);
    gtk_box_append(GTK_BOX(vbox), btn);

    /* file-chooser callbacks: each needs the corresponding entry as user_data */
    g_signal_connect(btn_term, "clicked", G_CALLBACK(on_choose_desktop_clicked), entry_term);
    g_signal_connect(btn_fm, "clicked", G_CALLBACK(on_choose_desktop_clicked), entry_fm);
    g_signal_connect(btn_browser, "clicked", G_CALLBACK(on_choose_desktop_clicked), entry_browser);
    g_signal_connect(btn_editor, "clicked", G_CALLBACK(on_choose_desktop_clicked), entry_editor);

    /* initialize current-default labels from xdg */
    char *v = query_xdg_mime_default("x-scheme-handler/terminal");
    if (v) { gtk_label_set_text(GTK_LABEL(cur_term), v); g_free(v); }
    v = query_xdg_mime_default("inode/directory");
    if (v) { gtk_label_set_text(GTK_LABEL(cur_fm), v); g_free(v); }
    v = query_xdg_mime_default("x-scheme-handler/http");
    if (v) { gtk_label_set_text(GTK_LABEL(cur_browser), v); g_free(v); }
    v = query_xdg_mime_default("text/plain");
    if (v) { gtk_label_set_text(GTK_LABEL(cur_editor), v); g_free(v); }

    return vbox;
}

/* ---------------- Binds editor page ---------------- */
typedef struct {
    GtkWidget  *container; /* GtkBox holding bind rows */
    GtkLabel   *status;    /* status label to report messages */
    char       *path;      /* path to binds.conf */
    GPtrArray  *rows;      /* array of GtkWidget* rows */
    GPtrArray  *original_lines; /* original file lines (preserve comments/blanks) */
} BindsPageData;

static void on_remove_bind_clicked(GtkButton *btn, gpointer user_data);
static void on_add_bind_clicked(GtkButton *btn, gpointer user_data);
static void on_save_binds_clicked(GtkButton *btn, gpointer user_data);

static GtkWidget *create_bind_row(BindsPageData *pd, const char *line)
{
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(h, TRUE);

    GtkWidget *entry = gtk_entry_new();
    if (line) gtk_editable_set_text(GTK_EDITABLE(entry), line);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(h), entry);

    GtkWidget *btn_rm = gtk_button_new_with_label("Remove");
    /* store pointer to row so handler can remove it */
    g_object_set_data(G_OBJECT(btn_rm), "row", h);
    g_signal_connect(btn_rm, "clicked", G_CALLBACK(on_remove_bind_clicked), h);
    gtk_box_append(GTK_BOX(h), btn_rm);

    /* keep backref to page data */
    g_object_set_data(G_OBJECT(h), "binds-pd", pd);

    return h;
}

/* For display, return an unmodified copy of the bind line. We intentionally
 * do not filter or remove tokens here (no bans). The loader still only
 * displays lines that begin with "bind". Caller must g_free the return. */
static char *sanitize_bind_for_display(const char *s)
{
    if (!s) return NULL;
    return g_strdup(s);
}

static void connect_bind_row_remove_button(GtkWidget *row)
{
    /* row's last child is remove button */
    GtkWidget *btn = gtk_widget_get_last_child(row);
    if (btn && GTK_IS_BUTTON(btn)) {
        g_signal_handlers_disconnect_by_func(btn, G_CALLBACK(on_remove_bind_clicked), row);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_remove_bind_clicked), row);
    }
}

static void load_binds_file(BindsPageData *pd)
{
    /* clear existing rows */
    if (pd->rows) {
        for (guint i = 0; i < pd->rows->len; ++i) {
            GtkWidget *r = g_ptr_array_index(pd->rows, i);
            gtk_box_remove(GTK_BOX(pd->container), r);
            /* let GTK free widgets when unreferenced by container */
        }
        g_ptr_array_free(pd->rows, TRUE);
        pd->rows = NULL;
    }

    gchar *content = NULL;
    if (!g_file_get_contents(pd->path, &content, NULL, NULL)) {
        set_status(pd->status, "No binds.conf found at %s — starting empty", pd->path);
        return;
    }

    gchar **lines = g_strsplit(content, "\n", -1);
    /* preserve all original lines so we can write them back unchanged
     * (comments/blank lines will be preserved in their positions).
     * Only create visible rows for actual bind lines (non-empty, not starting with '#'). */
    pd->original_lines = g_ptr_array_new_with_free_func(g_free);
    pd->rows = g_ptr_array_new();
    for (gint i = 0; lines[i] != NULL; ++i) {
        g_ptr_array_add(pd->original_lines, g_strdup(lines[i]));

        char *trim = g_strdup(lines[i]);
        g_strstrip(trim);
        if (strlen(trim) == 0) { g_free(trim); continue; }
        if (trim[0] == '#') { g_free(trim); continue; }

        /* Only treat lines that actually start with "bind" as editable bind
         * rows. Preserve other non-comment, non-empty lines (for example
         * lines beginning with '=' which are metadata) in original_lines but
         * do not show them as bind entries. */
        char *lower = g_ascii_strdown(trim, -1);
        gboolean is_bind = g_str_has_prefix(lower, "bind");
        g_free(lower);
        g_free(trim);
        if (!is_bind) continue;

        /* create a displayed row with $mainMod removed for clarity while
         * preserving the original line in pd->original_lines */
        char *disp = sanitize_bind_for_display(lines[i]);
        GtkWidget *row = create_bind_row(pd, disp ? disp : lines[i]);
        g_free(disp);
        gtk_box_append(GTK_BOX(pd->container), row);
        g_ptr_array_add(pd->rows, row);
        connect_bind_row_remove_button(row);
    }
    g_strfreev(lines);
    g_free(content);

    set_status(pd->status, "Loaded %s", pd->path);
}

static void on_remove_bind_clicked(GtkButton *btn, gpointer user_data)
{
    GtkWidget *row = GTK_WIDGET(user_data);
    GtkWidget *parent = gtk_widget_get_parent(row);
    if (parent && GTK_IS_BOX(parent)) {
        /* remove from container */
        gtk_box_remove(GTK_BOX(parent), row);
        /* also remove from pd->rows if present */
        BindsPageData *pd = g_object_get_data(G_OBJECT(row), "binds-pd");
        if (pd && pd->rows) {
            /* g_ptr_array_remove will remove the first occurrence */
            g_ptr_array_remove(pd->rows, row);
        }
        /* Do not explicitly destroy row here; GTK will free widget resources
         * once references are dropped. Avoid calling gtk_widget_destroy to
         * remain compatible with different GTK headers/environments. */
    }
}

static void on_add_bind_clicked(GtkButton *btn, gpointer user_data)
{
    BindsPageData *pd = user_data;
    GtkWidget *row = create_bind_row(pd, "");
    connect_bind_row_remove_button(row);
    gtk_box_append(GTK_BOX(pd->container), row);
    if (!pd->rows) pd->rows = g_ptr_array_new();
    g_ptr_array_add(pd->rows, row);
    set_status(pd->status, "Added new bind entry (edit and Save)");
}

static void on_add_comment_clicked(GtkButton *btn, gpointer user_data)
{
    BindsPageData *pd = user_data;
    GtkWidget *row = create_bind_row(pd, "# ");
    connect_bind_row_remove_button(row);
    gtk_box_append(GTK_BOX(pd->container), row);
    if (!pd->rows) pd->rows = g_ptr_array_new();
    g_ptr_array_add(pd->rows, row);
    set_status(pd->status, "Added new comment (edit and Save)");
}

static void on_save_binds_clicked(GtkButton *btn, gpointer user_data)
{
    BindsPageData *pd = user_data;
    /* Validate each non-empty, non-comment line starts with "bind" (case-insensitive).
     * Visually mark invalid entries with the 'invalid-entry' CSS class and focus
     * the first offending entry. If validation passes, collect lines and write the file. */
    GString *out = g_string_new(NULL);
    GtkWidget *first_invalid = NULL;
    /* Collect visible lines and validate them. We'll preserve original
     * comments/blanks by reconstructing the file using pd->original_lines. */
    GPtrArray *visible_bind_lines = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *visible_comment_lines = g_ptr_array_new_with_free_func(g_free);
    if (pd->rows) {
        for (guint i = 0; i < pd->rows->len; ++i) {
            GtkWidget *row = g_ptr_array_index(pd->rows, i);
            GtkWidget *entry = gtk_widget_get_first_child(row);
            if (entry && GTK_IS_ENTRY(entry)) {
                /* clear previous invalid marker */
                GtkStyleContext *ctx = gtk_widget_get_style_context(entry);
                gtk_style_context_remove_class(ctx, "invalid-entry");

                const char *txt = gtk_editable_get_text(GTK_EDITABLE(entry));
                const char *use_txt = txt ? txt : "";

                /* Trim whitespace for validation */
                char *trim = g_strdup(use_txt);
                g_strstrip(trim);

                if (strlen(trim) == 0) {
                    /* treat empty visible entries as empty strings appended later */
                    g_ptr_array_add(visible_bind_lines, g_strdup(""));
                    g_free(trim);
                    continue;
                }

                if (trim[0] == '#') {
                    /* comment row added by user */
                    g_ptr_array_add(visible_comment_lines, g_strdup(use_txt));
                    g_free(trim);
                    continue;
                }

                /* case-insensitive check for prefix 'bind' */
                char *lower = g_ascii_strdown(trim, -1);
                if (!g_str_has_prefix(lower, "bind")) {
                    /* mark invalid visually */
                    gtk_style_context_add_class(ctx, "invalid-entry");
                    if (!first_invalid) first_invalid = entry;
                    g_free(lower);
                    g_free(trim);
                    continue; /* continue checking others but don't save yet */
                }
                g_free(lower);
                g_free(trim);

                g_ptr_array_add(visible_bind_lines, g_strdup(use_txt));
            }
        }
    }

    if (first_invalid) {
        /* free temporary arrays (they have g_free as free_func) */
        g_ptr_array_free(visible_bind_lines, TRUE);
        g_ptr_array_free(visible_comment_lines, TRUE);
        set_status(pd->status, "Validation failed: some lines must start with 'bind'");
        gtk_widget_grab_focus(first_invalid);
        return;
    }

    /* Reconstruct output using original_lines: replace original bind lines in-place
     * with the visible bind lines in order; preserve original comments/blanks.
     * Any remaining visible bind lines are appended. Visible comment lines added
     * by the user are appended at the end (in insertion order). */
    GPtrArray *out_lines = g_ptr_array_new_with_free_func(g_free);
    if (pd->original_lines && pd->original_lines->len > 0) {
        /* copy originals */
        for (guint i = 0; i < pd->original_lines->len; ++i) {
            g_ptr_array_add(out_lines, g_strdup(g_ptr_array_index(pd->original_lines, i)));
        }
    }

    guint bind_index = 0;
    for (guint i = 0; i < out_lines->len; ++i) {
        char *oline = g_strdup(g_ptr_array_index(out_lines, i));
        char *trim = g_strdup(oline);
        g_strstrip(trim);
        if (strlen(trim) == 0 || trim[0] == '#') {
            /* keep as-is */
            g_free(oline);
            g_free(trim);
            continue;
        }

        /* Only treat originals that actually start with 'bind' as bind
         * lines to replace. Other non-comment lines (e.g. metadata like
         * "= SUPER ...") should be preserved untouched. */
        char *lower = g_ascii_strdown(trim, -1);
        gboolean orig_is_bind = g_str_has_prefix(lower, "bind");
        g_free(lower);
        if (!orig_is_bind) {
            /* keep as-is */
            g_free(oline);
            g_free(trim);
            continue;
        }

        /* original was a bind line; replace with next visible bind or empty */
        g_free(g_ptr_array_index(out_lines, i));
        if (bind_index < visible_bind_lines->len) {
            g_ptr_array_index(out_lines, i) = g_strdup(g_ptr_array_index(visible_bind_lines, bind_index));
            bind_index++;
        } else {
            g_ptr_array_index(out_lines, i) = g_strdup("");
        }
        g_free(oline);
        g_free(trim);
    }

    /* append remaining visible bind lines */
    while (bind_index < visible_bind_lines->len) {
        g_ptr_array_add(out_lines, g_strdup(g_ptr_array_index(visible_bind_lines, bind_index)));
        bind_index++;
    }

    /* append visible comments added by the user */
    for (guint i = 0; i < visible_comment_lines->len; ++i) {
        g_ptr_array_add(out_lines, g_strdup(g_ptr_array_index(visible_comment_lines, i)));
    }

    /* join into a single string */
    GString *final = g_string_new(NULL);
    for (guint i = 0; i < out_lines->len; ++i) {
        char *s = g_ptr_array_index(out_lines, i);
        g_string_append(final, s ? s : "");
        if (i + 1 < out_lines->len) g_string_append_c(final, '\n');
    }

    /* cleanup temporary arrays (out_lines and visible_* use g_free as their free func) */
    g_ptr_array_free(visible_bind_lines, TRUE);
    g_ptr_array_free(visible_comment_lines, TRUE);
    g_ptr_array_free(out_lines, TRUE);
    g_string_free(out, TRUE);
    out = final;

    if (first_invalid) {
        set_status(pd->status, "Validation failed: some lines must start with 'bind'");
        gtk_widget_grab_focus(first_invalid);
        g_string_free(out, TRUE);
        return;
    }

    GError *error = NULL;
    if (!g_file_set_contents(pd->path, out->str, -1, &error)) {
        set_status(pd->status, "Failed to write %s: %s", pd->path, error->message);
        g_clear_error(&error);
        g_string_free(out, TRUE);
        return;
    }
    g_string_free(out, TRUE);

    set_status(pd->status, "Saved binds to %s", pd->path);
    /* Try to reload Hyprland if hyprctl exists and show its output in a dialog */
    char *hyprctl = g_find_program_in_path("hyprctl");
    if (hyprctl) {
        char *cmd = g_strdup_printf("%s reload", hyprctl);
        gchar *out = NULL;
        gchar *err = NULL;
        gint exit_status = 0;
        GError *gerr = NULL;
        gboolean ok = g_spawn_command_line_sync(cmd, &out, &err, &exit_status, &gerr);
        if (!ok) {
            set_status(pd->status, "Failed to run hyprctl: %s", gerr ? gerr->message : "unknown");
            if (gerr) g_clear_error(&gerr);
            g_free(cmd);
            g_free(hyprctl);
            g_free(out);
            g_free(err);
            return;
        }

    /* show output in a modal dialog (no transient parent available here) */
    GtkWidget *dlg = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dlg), "hyprctl reload output");
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
        gtk_window_set_default_size(GTK_WINDOW(dlg), 600, 400);

        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_margin_start(v, 8);
        gtk_widget_set_margin_end(v, 8);
        gtk_widget_set_margin_top(v, 8);
        gtk_widget_set_margin_bottom(v, 8);
        gtk_window_set_child(GTK_WINDOW(dlg), v);

        GtkWidget *info = gtk_label_new(NULL);
        char status_msg[128];
        if (WIFEXITED(exit_status)) {
            snprintf(status_msg, sizeof(status_msg), "hyprctl exit: %d", WEXITSTATUS(exit_status));
        } else if (WIFSIGNALED(exit_status)) {
            snprintf(status_msg, sizeof(status_msg), "hyprctl killed by signal %d", WTERMSIG(exit_status));
        } else {
            snprintf(status_msg, sizeof(status_msg), "hyprctl exit status: %d", exit_status);
        }
        gtk_label_set_text(GTK_LABEL(info), status_msg);
        gtk_box_append(GTK_BOX(v), info);

        GtkWidget *sc = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(sc, TRUE);
        gtk_widget_set_hexpand(sc, TRUE);
        gtk_box_append(GTK_BOX(v), sc);

        GtkWidget *tv = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
        GString *buftxt = g_string_new(NULL);
        if (out && *out) {
            g_string_append(buftxt, "STDOUT:\n");
            g_string_append(buftxt, out);
            g_string_append(buftxt, "\n");
        }
        if (err && *err) {
            g_string_append(buftxt, "STDERR:\n");
            g_string_append(buftxt, err);
            g_string_append(buftxt, "\n");
        }
        if (buftxt->len == 0) g_string_append(buftxt, "(no output)\n");
        gtk_text_buffer_set_text(buf, buftxt->str, -1);
        g_string_free(buftxt, TRUE);

        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), tv);

        GtkWidget *btn_close = gtk_button_new_with_label("Close");
        g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_destroy), dlg);
        gtk_box_append(GTK_BOX(v), btn_close);

        gtk_window_present(GTK_WINDOW(dlg));

        g_free(cmd);
        g_free(hyprctl);
        g_free(out);
        g_free(err);
    } else {
        set_status(pd->status, "hyprctl not found; edit your config or restart Hyprland to apply");
    }
}

static GtkWidget *create_binds_page(GtkLabel *status_label)
{
    /* Ensure CSS class for invalid entries exists (one-time). This makes
     * entries visually indicate validation errors. */
    static gboolean css_installed = FALSE;
    if (!css_installed) {
        GtkCssProvider *prov = gtk_css_provider_new();
        const char *css = ".invalid-entry { background-color: #ffdddd; }\n";
        gtk_css_provider_load_from_data(prov, css, -1);
        GdkDisplay *dpy = gdk_display_get_default();
        if (dpy) gtk_style_context_add_provider_for_display(dpy, GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);
        css_installed = TRUE;
    }

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Hyprland Binds Editor");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Edit your Hyprland binds (one line per bind). Use Add to create a new bind row, Remove to delete a bind, and Save to write to ~/.config/hypr/binds.conf and reload Hyprland.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroller);

    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(container, 6);
    gtk_widget_set_margin_bottom(container, 6);
    gtk_widget_set_hexpand(container, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), container);

    /* prepare data */
    BindsPageData *pd = g_new0(BindsPageData, 1);
    pd->container = container;
    pd->status = GTK_LABEL(status_label);
    pd->path = g_build_filename(g_get_home_dir(), ".config", "hypr", "binds.conf", NULL);

    /* Add controls */
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_add = gtk_button_new_with_label("Add bind");
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_bind_clicked), pd);
    gtk_box_append(GTK_BOX(h), btn_add);

    GtkWidget *btn_comment = gtk_button_new_with_label("Add comment");
    g_signal_connect(btn_comment, "clicked", G_CALLBACK(on_add_comment_clicked), pd);
    gtk_box_append(GTK_BOX(h), btn_comment);

    GtkWidget *btn_save = gtk_button_new_with_label("Save binds");
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_binds_clicked), pd);
    gtk_box_append(GTK_BOX(h), btn_save);
    
    /* align control buttons to the end so they don't crowd the left column */
    gtk_widget_set_halign(h, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), h);

    /* load existing binds */
    load_binds_file(pd);

    return vbox;
}

/* Helper: find a suitable sudo group name (sudo or wheel) */
static const char *find_sudo_group(void)
{
    struct group *g;
    g = getgrnam("sudo");
    if (g) return "sudo";
    g = getgrnam("wheel");
    if (g) return "wheel";
    return NULL;
}

/* Create a temporary script with content and run it with pkexec if available.
 * Returns TRUE if we launched the privileged command, FALSE otherwise. */
/* (previous temp-file based privileged flow removed) */

/* Generic child-watch data for processes where we only need to report status. */
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

/* (old privileged-child exit handler removed; we use generic child watcher for pkexec-with-stdin) */

static gboolean run_privileged_script(const char *script_contents, GtkLabel *status)
{
    /* Use pkexec and pass the script on stdin to /bin/bash -s to avoid
     * writing sensitive data to a temporary file. */
    return run_command_via_pkexec_stdin(script_contents, status);
}

/* Run a command via pkexec and pass the command on stdin to /bin/bash -s.
 * Returns TRUE if the launch succeeded. The child-exit watcher will report
 * completion to the status label. */
static gboolean run_command_via_pkexec_stdin(const char *command, GtkLabel *status)
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

    /* write the command to the child's stdin and close it */
    write(stdin_fd, command, strlen(command));
    write(stdin_fd, "\n", 1);
    close(stdin_fd);

    /* attach a generic child-watch to report completion */
    ChildWatchData *cd = g_new0(ChildWatchData, 1);
    cd->status = status;
    g_child_watch_add(child_pid, on_child_exit, cd);

    set_status(status, "Launched privileged command via pkexec; authentication may be requested.");

    g_free(pkexec_path);
    return TRUE;
}

/* Adapter: button clicked -> open config dir stored on the button */
static void on_config_button_clicked(GtkButton *btn, gpointer user_data)
{
    const char *subdir = g_object_get_data(G_OBJECT(btn), "config-subdir");
    GtkLabel *status = GTK_LABEL(g_object_get_data(G_OBJECT(btn), "status"));
    if (!subdir) return;
    open_config_dir(subdir, status);
}

/* Adapter: button clicked -> launch program stored on the button */
static void on_launch_button_clicked(GtkButton *btn, gpointer user_data)
{
    const char *prog = g_object_get_data(G_OBJECT(btn), "prog");
    GtkLabel *status = GTK_LABEL(g_object_get_data(G_OBJECT(btn), "status"));
    if (!prog) return;
    launch_if_found(prog, status);
}

/* Utilities */
static gboolean user_exists(const char *username)
{
    struct passwd *pw = getpwnam(username);
    return pw != NULL;
}

/* Dialog windows: create simple modal window with entries and buttons */
static GtkWindow *create_modal_window(GtkWindow *parent, const char *title)
{
    GtkWindow *w = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(w, title);
    gtk_window_set_transient_for(w, parent);
    gtk_window_set_modal(w, TRUE);
    gtk_window_set_default_size(w, 400, 200);
    return w;
}

/* Add user UI and handler */
static void on_add_user_submit(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkWindow *dialog = GTK_WINDOW(ud[0]);
    GtkEntry *entry_user = GTK_ENTRY(ud[1]);
    GtkEntry *entry_pass = GTK_ENTRY(ud[2]);
    GtkCheckButton *chk_sudo = GTK_CHECK_BUTTON(ud[3]);
    GtkCheckButton *chk_passless = GTK_CHECK_BUTTON(ud[4]);
    GtkLabel *status = GTK_LABEL(ud[5]);

    const char *username = gtk_editable_get_text(GTK_EDITABLE(entry_user));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(entry_pass));
    gboolean add_sudo = gtk_check_button_get_active(chk_sudo);
    gboolean passless = gtk_check_button_get_active(chk_passless);

    if (!username || strlen(username) == 0) {
        set_status(status, "Please enter a username");
        return;
    }
    if (user_exists(username)) {
        set_status(status, "User '%s' already exists", username);
        return;
    }

    /* Build script */
    char *sq_user = g_shell_quote(username);
    char *sq_pass = g_shell_quote(password ? password : "");

    const char *sudo_group = find_sudo_group();
    char *script = NULL;
    if (add_sudo && sudo_group) {
        if (passless) {
            script = g_strdup_printf(
                "#!/bin/bash\nset -e\nuseradd -m -s /bin/zsh %s\necho %s:%s | chpasswd\nusermod -aG %s %s\necho %s ALL=(ALL) NOPASSWD: ALL > /etc/sudoers.d/%s\nchmod 0440 /etc/sudoers.d/%s\necho 'OK'\n",
                sq_user, sq_user, sq_pass, sudo_group, sq_user, sq_user, sq_user, sq_user);
        } else {
            script = g_strdup_printf(
                "#!/bin/bash\nset -e\nuseradd -m -s /bin/zsh %s\necho %s:%s | chpasswd\nusermod -aG %s %s\necho 'OK'\n",
                sq_user, sq_user, sq_pass, sudo_group, sq_user);
        }
    } else if (add_sudo && !sudo_group) {
        /* no sudo group found; still create user */
        script = g_strdup_printf(
            "#!/bin/bash\nset -e\nuseradd -m -s /bin/zsh %s\necho %s:%s | chpasswd\necho 'OK'\n",
            sq_user, sq_user, sq_pass);
    } else {
        script = g_strdup_printf(
            "#!/bin/bash\nset -e\nuseradd -m -s /bin/zsh %s\necho %s:%s | chpasswd\necho 'OK'\n",
            sq_user, sq_user, sq_pass);
    }

    gboolean launched = run_privileged_script(script, status);

    g_free(sq_user);
    g_free(sq_pass);
    g_free(script);

    if (launched) {
        set_status(status, "Add user command launched");
        gtk_window_destroy(dialog);
    }
}

static void on_add_user_cancel(GtkButton *btn, gpointer user_data)
{
    GtkWindow *dialog = GTK_WINDOW(user_data);
    gtk_window_destroy(dialog);
}

static void open_add_user_dialog(GtkWindow *parent, GtkLabel *status)
{
    GtkWindow *dialog = create_modal_window(parent, "Add User");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(dialog, vbox);

    GtkWidget *entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_user), "username");
    gtk_box_append(GTK_BOX(vbox), entry_user);

    GtkWidget *entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "password");
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    gtk_box_append(GTK_BOX(vbox), entry_pass);

    GtkWidget *chk_sudo = gtk_check_button_new_with_label("Add to sudo/wheel group");
    gtk_box_append(GTK_BOX(vbox), chk_sudo);
    GtkWidget *chk_passless = gtk_check_button_new_with_label("Enable passwordless sudo");
    gtk_box_append(GTK_BOX(vbox), chk_passless);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_ok = gtk_button_new_with_label("Add");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(hbox), btn_ok);
    gtk_box_append(GTK_BOX(hbox), btn_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gpointer *ud = g_new(gpointer, 6);
    ud[0] = dialog;
    ud[1] = entry_user;
    ud[2] = entry_pass;
    ud[3] = chk_sudo;
    ud[4] = chk_passless;
    ud[5] = status;

    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_add_user_submit), ud);
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_add_user_cancel), dialog);

    gtk_window_present(dialog);
}

/* Delete user */
static void on_delete_user_submit(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkWindow *dialog = GTK_WINDOW(ud[0]);
    GtkEntry *entry_user = GTK_ENTRY(ud[1]);
    GtkCheckButton *chk_remove_home = GTK_CHECK_BUTTON(ud[2]);
    GtkLabel *status = GTK_LABEL(ud[3]);

    const char *username = gtk_editable_get_text(GTK_EDITABLE(entry_user));
    gboolean remove_home = gtk_check_button_get_active(chk_remove_home);

    if (!username || strlen(username) == 0) {
        set_status(status, "Please enter a username");
        return;
    }
    if (!user_exists(username)) {
        set_status(status, "User '%s' does not exist", username);
        return;
    }

    char *sq_user = g_shell_quote(username);
    char *script = NULL;
    if (remove_home) {
        script = g_strdup_printf("#!/bin/bash\nset -e\nuserdel -r %s\nrm -f /etc/sudoers.d/%s\necho 'OK'\n", sq_user, sq_user);
    } else {
        script = g_strdup_printf("#!/bin/bash\nset -e\nuserdel %s\nrm -f /etc/sudoers.d/%s\necho 'OK'\n", sq_user, sq_user);
    }

    gboolean launched = run_privileged_script(script, status);
    g_free(sq_user);
    g_free(script);

    if (launched) {
        set_status(status, "Delete user command launched");
        gtk_window_destroy(dialog);
    }
}

static void on_delete_user_cancel(GtkButton *btn, gpointer user_data)
{
    GtkWindow *dialog = GTK_WINDOW(user_data);
    gtk_window_destroy(dialog);
}

static void open_delete_user_dialog(GtkWindow *parent, GtkLabel *status)
{
    GtkWindow *dialog = create_modal_window(parent, "Delete User");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(dialog, vbox);

    GtkWidget *entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_user), "username");
    gtk_box_append(GTK_BOX(vbox), entry_user);

    GtkWidget *chk_remove_home = gtk_check_button_new_with_label("Remove home directory");
    gtk_box_append(GTK_BOX(vbox), chk_remove_home);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_ok = gtk_button_new_with_label("Delete");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(hbox), btn_ok);
    gtk_box_append(GTK_BOX(hbox), btn_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gpointer *ud = g_new(gpointer, 4);
    ud[0] = dialog;
    ud[1] = entry_user;
    ud[2] = chk_remove_home;
    ud[3] = status;

    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_delete_user_submit), ud);
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_delete_user_cancel), dialog);

    gtk_window_present(dialog);
}

/* Change password dialog */
static void on_change_pass_submit(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkWindow *dialog = GTK_WINDOW(ud[0]);
    GtkEntry *entry_user = GTK_ENTRY(ud[1]);
    GtkEntry *entry_pass = GTK_ENTRY(ud[2]);
    GtkLabel *status = GTK_LABEL(ud[3]);

    const char *username = gtk_editable_get_text(GTK_EDITABLE(entry_user));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(entry_pass));

    if (!username || strlen(username) == 0) {
        set_status(status, "Please enter a username");
        return;
    }
    if (!user_exists(username)) {
        set_status(status, "User '%s' does not exist", username);
        return;
    }

    char *sq_user = g_shell_quote(username);
    char *sq_pass = g_shell_quote(password ? password : "");
    char *script = g_strdup_printf("#!/bin/bash\nset -e\necho %s:%s | chpasswd\necho 'OK'\n", sq_user, sq_pass);

    gboolean launched = run_privileged_script(script, status);
    g_free(sq_user);
    g_free(sq_pass);
    g_free(script);

    if (launched) {
        set_status(status, "Change password command launched");
        gtk_window_destroy(dialog);
    }
}

static void on_change_pass_cancel(GtkButton *btn, gpointer user_data)
{
    GtkWindow *dialog = GTK_WINDOW(user_data);
    gtk_window_destroy(dialog);
}

static void open_change_pass_dialog(GtkWindow *parent, GtkLabel *status)
{
    GtkWindow *dialog = create_modal_window(parent, "Change Password");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(dialog, vbox);

    GtkWidget *entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_user), "username");
    gtk_box_append(GTK_BOX(vbox), entry_user);

    GtkWidget *entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "new password");
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    gtk_box_append(GTK_BOX(vbox), entry_pass);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_ok = gtk_button_new_with_label("Change");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(hbox), btn_ok);
    gtk_box_append(GTK_BOX(hbox), btn_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gpointer *ud = g_new(gpointer, 4);
    ud[0] = dialog;
    ud[1] = entry_user;
    ud[2] = entry_pass;
    ud[3] = status;

    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_change_pass_submit), ud);
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_change_pass_cancel), dialog);

    gtk_window_present(dialog);
}

static GtkWidget *create_packages_page(GtkWindow *parent, GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Package name:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "e.g. firefox");
    gtk_box_append(GTK_BOX(vbox), entry);

    /* Removed per-request: dry-run toggle moved to --dry-run command line flag */

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_box_append(GTK_BOX(vbox), grid);

    const char *btn_labels[] = {"Install (AUR)", "Install (Pacman)", "Install (Flatpak)", "Update (AUR + Flatpak)"};
    const char *actions[] = {"aur", "pacman", "flatpak", "update_aur_flatpak"};

    PackageActionData *pdata = g_new0(PackageActionData, 1);
    pdata->parent = parent;
    pdata->entry = GTK_ENTRY(entry);
    pdata->status_label = status_label;

    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(btn_labels[i]);
        /* store a duplicated action string and let GLib free it if the widget is destroyed */
        g_object_set_data_full(G_OBJECT(btn), "action", g_strdup(actions[i]), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_package_action), pdata);
        gtk_grid_attach(GTK_GRID(grid), btn, i % 2, i / 2, 1, 1);
    }

    return vbox;
}

static void on_open_users_manager(GtkButton *button, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    /* Prefer an installed 'userctl' binary and run it via sudo in the
     * user's preferred terminal. If it's not present, fall back to the
     * legacy 'old script.sh' in the current directory. */
    char *userctl_path = g_find_program_in_path("userctl");
    if (userctl_path) {
        char *prefix = get_terminal_prefix();
        char *quoted = g_shell_quote(userctl_path);
        char *inner = g_strdup_printf("sudo %s", quoted);
        char *cmd = g_strdup_printf(prefix, inner);

        run_command_and_report(cmd, status);

        g_free(prefix);
        g_free(quoted);
        g_free(inner);
        g_free(cmd);
        g_free(userctl_path);
        return;
    }

    /* fallback: run legacy script if present */
    gchar *script = g_build_filename(g_get_current_dir(), "old script.sh", NULL);
    if (!g_file_test(script, G_FILE_TEST_EXISTS)) {
        set_status(status, "Neither 'userctl' nor legacy script found");
        g_free(script);
        return;
    }

    char *prefix = get_terminal_prefix();
    char *quoted = g_shell_quote(script);
    char *inner = g_strdup_printf("bash %s", quoted);
    char *cmd = g_strdup_printf(prefix, inner);

    run_command_and_report(cmd, status);

    g_free(prefix);
    g_free(quoted);
    g_free(inner);
    g_free(cmd);
    g_free(script);
}

static GtkWidget *create_users_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Users manager");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *btn_add = gtk_button_new_with_label("Add User");
    g_signal_connect(btn_add, "clicked", G_CALLBACK(open_add_user_dialog), status_label);
    gtk_box_append(GTK_BOX(vbox), btn_add);

    GtkWidget *btn_delete = gtk_button_new_with_label("Delete User");
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(open_delete_user_dialog), status_label);
    gtk_box_append(GTK_BOX(vbox), btn_delete);

    GtkWidget *btn_pass = gtk_button_new_with_label("Change Password");
    g_signal_connect(btn_pass, "clicked", G_CALLBACK(open_change_pass_dialog), status_label);
    gtk_box_append(GTK_BOX(vbox), btn_pass);

    /* legacy script launcher (kept as fallback) */
    GtkWidget *btn_legacy = gtk_button_new_with_label("Open TUI Users Manager (legacy)");
    g_signal_connect(btn_legacy, "clicked", G_CALLBACK(on_open_users_manager), status_label);
    gtk_box_append(GTK_BOX(vbox), btn_legacy);

    return vbox;
}

static GtkWidget *create_config_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Configuration & Utilities");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Quick access to common config directories and utilities. Open folders in your file manager or run the settings utilities below.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    /* Config folder buttons */
    GtkWidget *h1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_hypr = gtk_button_new_with_label("Open ~/.config/hypr");
    /* store subdir and status on the button so the adapter can call open_config_dir */
    g_object_set_data(G_OBJECT(btn_hypr), "config-subdir", (gpointer)"hypr");
    g_object_set_data(G_OBJECT(btn_hypr), "status", status_label);
    g_signal_connect(btn_hypr, "clicked", G_CALLBACK(on_config_button_clicked), NULL);
    gtk_box_append(GTK_BOX(h1), btn_hypr);
    GtkWidget *btn_waybar_cfg = gtk_button_new_with_label("Open ~/.config/waybar");
    g_object_set_data(G_OBJECT(btn_waybar_cfg), "config-subdir", (gpointer)"waybar");
    g_object_set_data(G_OBJECT(btn_waybar_cfg), "status", status_label);
    g_signal_connect(btn_waybar_cfg, "clicked", G_CALLBACK(on_config_button_clicked), NULL);
    gtk_box_append(GTK_BOX(h1), btn_waybar_cfg);
    GtkWidget *btn_rofi_cfg = gtk_button_new_with_label("Open ~/.config/rofi");
    g_object_set_data(G_OBJECT(btn_rofi_cfg), "config-subdir", (gpointer)"rofi");
    g_object_set_data(G_OBJECT(btn_rofi_cfg), "status", status_label);
    g_signal_connect(btn_rofi_cfg, "clicked", G_CALLBACK(on_config_button_clicked), NULL);
    gtk_box_append(GTK_BOX(h1), btn_rofi_cfg);
    gtk_box_append(GTK_BOX(vbox), h1);

    /* Utilities row */
    GtkWidget *h2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_audio = gtk_button_new_with_label("Audio settings (pavucontrol)");
    gtk_box_append(GTK_BOX(h2), btn_audio);

    GtkWidget *btn_waypaper = gtk_button_new_with_label("Wallpaper (waypaper)");
    g_signal_connect(btn_waypaper, "clicked", G_CALLBACK(on_run_waypaper), status_label);
    gtk_box_append(GTK_BOX(h2), btn_waypaper);

    GtkWidget *btn_disks = gtk_button_new_with_label("Disks (gnome-disks)");
    gtk_box_append(GTK_BOX(h2), btn_disks);

    GtkWidget *btn_thunar = gtk_button_new_with_label("File manager settings (thunar-settings)");
    gtk_box_append(GTK_BOX(h2), btn_thunar);

    gtk_box_append(GTK_BOX(vbox), h2);

    /* For buttons that use g_object_get_data to retrieve parameters, wrap their
     * clicked handlers with small adapters that extract the stored data. */
    /* Connect utility buttons to launch the named programs (use adapters) */
    g_object_set_data(G_OBJECT(btn_audio), "prog", (gpointer)"pavucontrol");
    g_object_set_data(G_OBJECT(btn_audio), "status", status_label);
    g_signal_connect(btn_audio, "clicked", G_CALLBACK(on_launch_button_clicked), NULL);

    g_object_set_data(G_OBJECT(btn_disks), "prog", (gpointer)"gnome-disks");
    g_object_set_data(G_OBJECT(btn_disks), "status", status_label);
    g_signal_connect(btn_disks, "clicked", G_CALLBACK(on_launch_button_clicked), NULL);

    g_object_set_data(G_OBJECT(btn_thunar), "prog", (gpointer)"thunar-settings");
    g_object_set_data(G_OBJECT(btn_thunar), "status", status_label);
    g_signal_connect(btn_thunar, "clicked", G_CALLBACK(on_launch_button_clicked), NULL);

    return vbox;
}

/* Hyprland editor implemented in hypr.c */

static void on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    GtkStack *stack = GTK_STACK(user_data);
    if (!row) return;
    GtkWidget *child = gtk_list_box_row_get_child(row);
    if (!GTK_IS_LABEL(child)) return;
    const char *text = gtk_label_get_text(GTK_LABEL(child));
    gtk_stack_set_visible_child_name(stack, text);
}

static void on_activate(GApplication *app, gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(window, "AserDev Settings");
    gtk_window_set_default_size(window, 900, 520);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_window_set_child(window, hbox);

    /* left list */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(left, GTK_ALIGN_START);
    /* keep the left sidebar a reasonable width on large screens but allow
     * the main stack to expand: request a modest width for the left column */
    gtk_widget_set_size_request(left, 200, -1);
    gtk_widget_set_hexpand(left, FALSE);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);

    GtkWidget *row1 = gtk_label_new("Packages");
    GtkWidget *row2 = gtk_label_new("Users");
    GtkWidget *row3 = gtk_label_new("Config");
    GtkWidget *row_hypr = gtk_label_new("Hyprland");
    GtkWidget *row4 = gtk_label_new("Clipboard");
    GtkWidget *row5 = gtk_label_new("Screen Recording");
    GtkWidget *row6 = gtk_label_new("Emoji");
    GtkWidget *row7 = gtk_label_new("Run App");
    GtkWidget *row8 = gtk_label_new("Run Command");
    GtkWidget *row9 = gtk_label_new("Binds");
    GtkWidget *row10 = gtk_label_new("Default Apps");

    gtk_list_box_insert(GTK_LIST_BOX(list), row1, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row2, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row3, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row_hypr, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row4, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row5, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row6, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row7, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row8, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row9, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row10, -1);
    gtk_box_append(GTK_BOX(left), list);

    gtk_box_append(GTK_BOX(hbox), left);

    /* right: stack + status */
    GtkWidget *right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_set_spacing(GTK_BOX(right_vbox), 6);
    gtk_widget_set_hexpand(right_vbox, TRUE);
    gtk_widget_set_vexpand(right_vbox, TRUE);

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);

    GtkWidget *status_label = gtk_label_new("");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);

    GtkWidget *pkg_page = create_packages_page(window, GTK_LABEL(status_label));
    GtkWidget *users_page = create_users_page(GTK_LABEL(status_label));
    GtkWidget *config_page = create_config_page(GTK_LABEL(status_label));
    GtkWidget *clipboard_page = create_clipboard_page(GTK_LABEL(status_label));
    GtkWidget *screenrec_page = create_screenrec_page(GTK_LABEL(status_label));
    GtkWidget *emoji_page = create_emoji_page(GTK_LABEL(status_label));
    GtkWidget *runapp_page = create_runapp_page(GTK_LABEL(status_label));
    GtkWidget *runcommand_page = create_runcommand_page(GTK_LABEL(status_label));
    GtkWidget *binds_page = create_binds_page(GTK_LABEL(status_label));
    GtkWidget *hyprland_page = create_hyprland_page(GTK_LABEL(status_label));
    GtkWidget *defaultapps_page = create_defaultapps_page(GTK_LABEL(status_label));

    gtk_stack_add_named(GTK_STACK(stack), pkg_page, "Packages");
    gtk_stack_add_named(GTK_STACK(stack), users_page, "Users");
    gtk_stack_add_named(GTK_STACK(stack), config_page, "Config");
    gtk_stack_add_named(GTK_STACK(stack), hyprland_page, "Hyprland");
    gtk_stack_add_named(GTK_STACK(stack), clipboard_page, "Clipboard");
    gtk_stack_add_named(GTK_STACK(stack), screenrec_page, "Screen Recording");
    gtk_stack_add_named(GTK_STACK(stack), emoji_page, "Emoji");
    gtk_stack_add_named(GTK_STACK(stack), runapp_page, "Run App");
    gtk_stack_add_named(GTK_STACK(stack), runcommand_page, "Run Command");
    gtk_stack_add_named(GTK_STACK(stack), binds_page, "Binds");
    gtk_stack_add_named(GTK_STACK(stack), defaultapps_page, "Default Apps");

    gtk_box_append(GTK_BOX(right_vbox), stack);
    gtk_box_append(GTK_BOX(right_vbox), status_label);
    gtk_box_append(GTK_BOX(hbox), right_vbox);

    g_signal_connect(list, "row-selected", G_CALLBACK(on_row_selected), stack);

    GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0);
    gtk_list_box_select_row(GTK_LIST_BOX(list), first);

    gtk_window_present(window);
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;
    /* parse --dry-run flag early so UI actions know behavior */
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--dry-run") == 0 || g_strcmp0(argv[i], "-n") == 0) {
            g_dry_run = TRUE;
        }
    }

    app = gtk_application_new("com.aserdev.settings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
