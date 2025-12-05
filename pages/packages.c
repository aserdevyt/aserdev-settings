#include "../common.h"
#include <gtk/gtk.h>

/*
 * Software Updates page
 * - large non-editable textview showing `yay -Qu`
 * - Refresh button: runs `yay -Syu --devel --timeupdate --needed --noconfirm`,
 *   waits for completion, then refreshes the text view
 * - Run /bin/update in a terminal button
 */

typedef struct {
    GtkTextView *tv;
    GtkLabel    *status;
} UpdatesRefreshData;

typedef struct {
    GtkTextView *tv;
    GtkLabel    *status;
    gchar       *text;
} UISetData;

static gboolean ui_set_text_cb(gpointer user_data)
{
    UISetData *d = (UISetData *)user_data;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(d->tv);
    gtk_text_buffer_set_text(buf, d->text ? d->text : "", -1);
    set_status(d->status, "Updated list");
    g_free(d->text);
    g_free(d);
    return G_SOURCE_REMOVE;
}

/* Thread: run `yay -Qu` and post results to UI */
static gpointer refresh_list_thread(gpointer user_data)
{
    UpdatesRefreshData *d = (UpdatesRefreshData *)user_data;

    if (g_dry_run) {
        UISetData *ud = g_new0(UISetData, 1);
        ud->tv = d->tv;
        ud->status = d->status;
        ud->text = g_strdup("Dry run: not executing 'yay -Qu'\n");
        g_idle_add(ui_set_text_cb, ud);
        g_free(d);
        return NULL;
    }

    gchar *out = NULL, *err = NULL;
    gint exit_status = 0;
    GError *error = NULL;

    gboolean ok = g_spawn_command_line_sync("yay -Qu", &out, &err, &exit_status, &error);
    gchar *text = NULL;
    if (!ok) {
        text = g_strdup_printf("Failed to run 'yay -Qu': %s\n", error ? error->message : "unknown");
    } else {
        if (out && *out) {
            text = g_strdup(out);
        } else {
            text = g_strdup("No updates available\n");
        }
    }

    UISetData *ud = g_new0(UISetData, 1);
    ud->tv = d->tv;
    ud->status = d->status;
    ud->text = text;
    g_idle_add(ui_set_text_cb, ud);

    if (out) g_free(out);
    if (err) g_free(err);
    if (error) g_clear_error(&error);
    g_free(d);
    return NULL;
}

static void start_refresh_list(GtkTextView *tv, GtkLabel *status)
{
    UpdatesRefreshData *d = g_new0(UpdatesRefreshData, 1);
    d->tv = tv;
    d->status = status;
    g_thread_new("updates-refresh", refresh_list_thread, d);
}

/* Thread: run the long update command then refresh the list */
static gpointer run_update_then_refresh_thread(gpointer user_data)
{
    UpdatesRefreshData *d = (UpdatesRefreshData *)user_data;

    if (g_dry_run) {
        g_idle_add((GSourceFunc)ui_set_text_cb, g_new0(UISetData, 1));
        set_status(d->status, "Dry run: skip running updates");
        g_free(d);
        return NULL;
    }

    gchar *out = NULL, *err = NULL;
    gint exit_status = 0;
    GError *error = NULL;

    set_status(d->status, "Running full system update (yay)...");
    gboolean ok = g_spawn_command_line_sync("yay -Syu --devel --timeupdate --needed --noconfirm", &out, &err, &exit_status, &error);

    if (!ok) {
        set_status(d->status, "Update failed to start: %s", error ? error->message : "unknown");
    } else if (exit_status != 0) {
        set_status(d->status, "Update command finished with exit %d", exit_status);
    } else {
        set_status(d->status, "Update completed successfully");
    }

    if (out) g_free(out);
    if (err) g_free(err);
    if (error) g_clear_error(&error);

    /* Refresh the list when done */
    UpdatesRefreshData *next = g_new0(UpdatesRefreshData, 1);
    next->tv = d->tv;
    next->status = d->status;
    g_thread_new("updates-refresh-after", refresh_list_thread, next);

    g_free(d);
    return NULL;
}

/* UI callbacks */
static void on_refresh_clicked(GtkButton *btn, gpointer user_data)
{
    UpdatesRefreshData *d = (UpdatesRefreshData *)user_data;
    if (g_dry_run) {
        set_status(d->status, "Dry run: not performing update");
        return;
    }
    UpdatesRefreshData *copy = g_new0(UpdatesRefreshData, 1);
    copy->tv = d->tv;
    copy->status = d->status;
    g_thread_new("run-update-thread", run_update_then_refresh_thread, copy);
}

static void on_run_update_terminal(GtkButton *btn, gpointer user_data)
{
    UpdatesRefreshData *d = (UpdatesRefreshData *)user_data;
    if (g_dry_run) {
        set_status(d->status, "Dry run: would open terminal to run /bin/update");
        return;
    }

    char *prefix = get_terminal_prefix();
    char *cmd = g_strdup_printf(prefix, "/bin/update");
    run_command_and_report(cmd, d->status);
    g_free(prefix);
    g_free(cmd);
}

GtkWidget *create_packages_page(GtkWindow *parent, GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    /* Large non-editable textview inside scrolled window */
    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sc, TRUE);

    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_hexpand(tv, TRUE);
    gtk_widget_set_vexpand(tv, TRUE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), tv);
    gtk_box_append(GTK_BOX(vbox), sc);

    /* Buttons at bottom: Refresh (runs full update) and Run /bin/update in terminal */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);

    UpdatesRefreshData *d = g_new0(UpdatesRefreshData, 1);
    d->tv = GTK_TEXT_VIEW(tv);
    d->status = status_label;

    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh (Run system update)");
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), d);
    gtk_box_append(GTK_BOX(hbox), btn_refresh);

    GtkWidget *btn_terminal = gtk_button_new_with_label("Run /bin/update in terminal");
    g_signal_connect(btn_terminal, "clicked", G_CALLBACK(on_run_update_terminal), d);
    gtk_box_append(GTK_BOX(hbox), btn_terminal);

    gtk_box_append(GTK_BOX(vbox), hbox);

    /* Initial population */
    start_refresh_list(GTK_TEXT_VIEW(tv), status_label);

    return vbox;
}
