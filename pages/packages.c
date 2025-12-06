#include "../common.h"
#include <gtk/gtk.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

/* forward declarations */
typedef struct EmbeddedUpdate EmbeddedUpdate;
static void on_pkexec_auth_done(GPid pid, gint status_code, gpointer user_data);

/* Dialog helper data and callbacks */
typedef struct { GMainLoop *loop; gboolean result; } DialogData;

static void on_yes_clicked(GtkButton *b, gpointer ud)
{
    DialogData *d = (DialogData *)ud;
    d->result = TRUE;
    g_main_loop_quit(d->loop);
}

static void on_no_clicked(GtkButton *b, gpointer ud)
{
    DialogData *d = (DialogData *)ud;
    d->result = FALSE;
    g_main_loop_quit(d->loop);
}

/* Simple synchronous yes/no modal implemented with GMainLoop to avoid deprecated gtk_dialog_run */
static gboolean ask_user_yes_no(GtkWindow *parent, const char *message)
{
    GtkWidget *win = gtk_window_new();
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 100);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_child(GTK_WINDOW(win), box);
    GtkWidget *lbl = gtk_label_new(message);
    gtk_box_append(GTK_BOX(box), lbl);

    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), h);
    GtkWidget *btn_yes = gtk_button_new_with_label("Yes");
    GtkWidget *btn_no = gtk_button_new_with_label("No");
    gtk_box_append(GTK_BOX(h), btn_yes);
    gtk_box_append(GTK_BOX(h), btn_no);

    DialogData *dd = g_new0(DialogData, 1);
    dd->loop = g_main_loop_new(NULL, FALSE);

    g_signal_connect(btn_yes, "clicked", G_CALLBACK(on_yes_clicked), dd);
    g_signal_connect(btn_no, "clicked", G_CALLBACK(on_no_clicked), dd);

    gtk_widget_show(win);
    g_main_loop_run(dd->loop);

    gboolean r = dd->result;
    g_main_loop_unref(dd->loop);
    g_free(dd);
    gtk_window_destroy(GTK_WINDOW(win));
    return r;
}

/* Data used to open the update terminal after pkexec authentication */
typedef struct {
    GtkLabel *status;
} PKExecAuthData;

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
    EmbeddedUpdate *embedded;
} UpdatesRefreshData;

/* Track embedded update process and widgets */
typedef struct EmbeddedUpdate {
    GPid child_pid;
    gint fd_out;
    gint fd_err;
    GIOChannel *ch_out;
    GIOChannel *ch_err;
    GtkTextView *out_tv;
    GtkWidget *stop_btn;
    gboolean running;
} EmbeddedUpdate;


/* Streaming state for the update window process */
typedef struct {
    GPid child_pid;
    gint fd_out;
    gint fd_err;
    GIOChannel *ch_out;
    GIOChannel *ch_err;
    GtkTextView *tv;
    GtkWidget *win;
    guint kill_timeout_id;
    gboolean stop_requested;
} StreamData;

static void append_text_to_tv(GtkTextView *tv, const char *text)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, text, -1);
    /* attempt to scroll to end by setting adjustment value */
    GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(tv));
    if (GTK_IS_SCROLLABLE(parent)) {
        GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(parent));
        if (adj) gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
    }
}

static gboolean stream_io_cb(GIOChannel *source, GIOCondition cond, gpointer ud)
{
    StreamData *s = (StreamData *)ud;
    GError *err = NULL;
    gchar *line = NULL;
    gsize len = 0;
    while (g_io_channel_read_line(source, &line, &len, NULL, &err) == G_IO_STATUS_NORMAL && line) {
        append_text_to_tv(s->tv, line);
        g_free(line);
    }
    if (err) {
        g_clear_error(&err);
    }
    if (cond & (G_IO_HUP | G_IO_ERR)) return G_SOURCE_REMOVE;
    return G_SOURCE_CONTINUE;
}

static void stream_child_watch(GPid pid, gint status_code, gpointer ud)
{
    StreamData *s = (StreamData *)ud;
    DBG("stream_child_watch called for pid=%d", pid);
    g_spawn_close_pid(pid);
    if (WIFEXITED(status_code)) {
        int es = WEXITSTATUS(status_code);
        char *msg = g_strdup_printf("\nProcess exited with status %d\n", es);
        append_text_to_tv(s->tv, msg);
        g_free(msg);
    } else if (WIFSIGNALED(status_code)) {
        char *msg = g_strdup_printf("\nProcess killed by signal %d\n", WTERMSIG(status_code));
        append_text_to_tv(s->tv, msg);
        g_free(msg);
    }
    if (s->ch_out) { g_io_channel_shutdown(s->ch_out, TRUE, NULL); g_io_channel_unref(s->ch_out); }
    if (s->ch_err) { g_io_channel_shutdown(s->ch_err, TRUE, NULL); g_io_channel_unref(s->ch_err); }
    if (s->fd_out >= 0) close(s->fd_out);
    if (s->fd_err >= 0) close(s->fd_err);
    /* keep window open so user can inspect output; user may close */
    g_free(s);
}

static gboolean stream_force_kill_cb(gpointer ud)
{
    StreamData *s = (StreamData *)ud;
    DBG("stream_force_kill_cb invoked for pid=%d", s ? s->child_pid : -1);
    if (!s) return G_SOURCE_REMOVE;
    if (s->child_pid > 0) {
        if (kill((pid_t)s->child_pid, SIGKILL) == 0) {
            append_text_to_tv(s->tv, "\nSent SIGKILL to process\n");
        } else {
            append_text_to_tv(s->tv, "\nFailed to send SIGKILL\n");
        }
    }
    s->kill_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static void stream_stop_clicked(GtkButton *b, gpointer ud)
{
    StreamData *s = (StreamData *)ud;
    DBG("stream_stop_clicked called, stop_requested=%d, pid=%d", s ? s->stop_requested : 0, s ? s->child_pid : -1);
    if (!s) return;
    if (s->stop_requested) {
        /* already requested, disable button */
        gtk_widget_set_sensitive(GTK_WIDGET(b), FALSE);
        return;
    }
    s->stop_requested = TRUE;
    if (s->child_pid > 0) {
        if (kill((pid_t)s->child_pid, SIGTERM) == 0) {
            append_text_to_tv(s->tv, "\nSent SIGTERM to process; will SIGKILL after 5s if still running\n");
            /* change label to indicate force-kill available */
            gtk_button_set_label(b, "Force Kill");
            /* schedule force-kill in 5 seconds */
            s->kill_timeout_id = g_timeout_add_seconds(5, stream_force_kill_cb, s);
        } else {
            append_text_to_tv(s->tv, "\nFailed to send SIGTERM\n");
            gtk_widget_set_sensitive(GTK_WIDGET(b), FALSE);
        }
    } else {
        append_text_to_tv(s->tv, "\nNo running process to abort\n");
        gtk_widget_set_sensitive(GTK_WIDGET(b), FALSE);
    }
}

/* Called when embedded update process exits: clear running state and update UI */
static void embedded_update_finished(EmbeddedUpdate *e, GtkLabel *status)
{
    if (!e) return;
    e->running = FALSE;
    if (e->ch_out) { g_io_channel_shutdown(e->ch_out, TRUE, NULL); g_io_channel_unref(e->ch_out); e->ch_out = NULL; }
    if (e->ch_err) { g_io_channel_shutdown(e->ch_err, TRUE, NULL); g_io_channel_unref(e->ch_err); e->ch_err = NULL; }
    if (e->fd_out >= 0) { close(e->fd_out); e->fd_out = -1; }
    if (e->fd_err >= 0) { close(e->fd_err); e->fd_err = -1; }
    if (e->stop_btn) gtk_widget_set_sensitive(e->stop_btn, FALSE);
    if (status) set_status(status, "System update finished");
}

static void on_embedded_child_exit(GPid pid, gint status_code, gpointer user_data)
{
    EmbeddedUpdate *e = (EmbeddedUpdate *)user_data;
    g_spawn_close_pid(pid);
    if (WIFEXITED(status_code)) {
        int es = WEXITSTATUS(status_code);
        char *msg = g_strdup_printf("\nProcess exited with status %d\n", es);
        append_text_to_tv(e->out_tv, msg);
        g_free(msg);
    } else if (WIFSIGNALED(status_code)) {
        char *msg = g_strdup_printf("\nProcess killed by signal %d\n", WTERMSIG(status_code));
        append_text_to_tv(e->out_tv, msg);
        g_free(msg);
    }
    /* cleanup and mark finished */
    embedded_update_finished(e, NULL);
    g_free(e);
}

/* Called when pkexec rm -f finishes; if successful, prompt for auth then start embedded update */
static void on_rm_child_done(GPid pid, gint status_code, gpointer user_data)
{
    UpdatesRefreshData *cd = (UpdatesRefreshData *)user_data;
    g_spawn_close_pid(pid);
    if (WIFEXITED(status_code) && WEXITSTATUS(status_code) == 0) {
        /* proceed to authenticate and start embedded update by triggering pkexec /bin/true */
        gchar *argv_auth[] = { "pkexec", "/bin/true", NULL };
        GError *err2 = NULL;
        GPid auth_pid;
        if (!g_spawn_async_with_pipes(NULL, argv_auth, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &auth_pid, NULL, NULL, NULL, &err2)) {
            set_status(cd->status, "Failed to start pkexec for auth: %s", err2 ? err2->message : "unknown");
            if (err2) g_clear_error(&err2);
            g_free(cd);
            return;
        }
        PKExecAuthData *ad = g_new0(PKExecAuthData, 1);
        ad->status = cd->status;
        g_child_watch_add(auth_pid, on_pkexec_auth_done, ad);
    } else {
        set_status(cd->status, "Failed to remove pacman lock; aborting update");
    }
    g_free(cd);
}

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
    /* Use the requested update command */
    const char *update_cmd = "yay -Sy --devel --timeupdate --needed --noconfirm";
    gboolean ok = g_spawn_command_line_sync(update_cmd, &out, &err, &exit_status, &error);

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

static void on_pkexec_auth_done(GPid pid, gint status_code, gpointer user_data)
{
    PKExecAuthData *d = (PKExecAuthData *)user_data;
    g_spawn_close_pid(pid);

    if (WIFEXITED(status_code) && WEXITSTATUS(status_code) == 0) {
        /* Auth succeeded, open a GTK window that runs pkexec pacman -Syu --noconfirm and shows live output */
        /* We'll spawn the actual update process and stream its stdout/stderr into a textview */
        GtkWidget *win = GTK_WIDGET(gtk_window_new());
        gtk_window_set_title(GTK_WINDOW(win), "System Update");
        gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_window_set_child(GTK_WINDOW(win), vbox);

        GtkWidget *sc = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_vexpand(sc, TRUE);

        GtkWidget *tv = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), tv);
        gtk_box_append(GTK_BOX(vbox), sc);

        GtkWidget *hbtn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *btn_stop = gtk_button_new_with_label("Abort");
        GtkWidget *btn_close = gtk_button_new_with_label("Close");
        gtk_box_append(GTK_BOX(hbtn), btn_stop);
        gtk_box_append(GTK_BOX(hbtn), btn_close);
        gtk_box_append(GTK_BOX(vbox), hbtn);

        /* Spawn pkexec pacman -Syu --noconfirm and capture stdout/stderr */
        gchar *argv[] = { "pkexec", "pacman", "-Syu", "--noconfirm", NULL };
        GError *error = NULL;
        GPid child_pid;
        gint fd_out = -1, fd_err = -1;

        if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &child_pid, &fd_out, &fd_err, NULL, &error)) {
            set_status(d->status, "Failed to start update: %s", error ? error->message : "unknown");
            g_clear_error(&error);
            gtk_window_destroy(GTK_WINDOW(win));
            g_free(d);
            return;
        }

        StreamData *sd = g_new0(StreamData, 1);
        sd->child_pid = child_pid;
        sd->fd_out = fd_out;
        sd->fd_err = fd_err;
        sd->tv = GTK_TEXT_VIEW(tv);
        sd->win = win;
        sd->ch_out = NULL;
        sd->ch_err = NULL;

        /* IO watch setup */
        GIOChannel *ch_out = g_io_channel_unix_new(fd_out);
        g_io_channel_set_encoding(ch_out, NULL, NULL);
        g_io_channel_set_flags(ch_out, G_IO_FLAG_NONBLOCK, NULL);
        sd->ch_out = ch_out;
        g_io_add_watch(ch_out, G_IO_IN | G_IO_HUP | G_IO_ERR, stream_io_cb, sd);

        GIOChannel *ch_err = g_io_channel_unix_new(fd_err);
        g_io_channel_set_encoding(ch_err, NULL, NULL);
        g_io_channel_set_flags(ch_err, G_IO_FLAG_NONBLOCK, NULL);
        sd->ch_err = ch_err;
        g_io_add_watch(ch_err, G_IO_IN | G_IO_HUP | G_IO_ERR, stream_io_cb, sd);

        /* child watch: when process exits, show status */
        g_child_watch_add(child_pid, stream_child_watch, sd);

        /* connect stop and close buttons */
        g_signal_connect(btn_stop, "clicked", G_CALLBACK(stream_stop_clicked), sd);
        g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_destroy), win);

        gtk_widget_show(win);
        set_status(d->status, "System update started in window");
    } else {
        set_status(d->status, "Authentication for system update was cancelled or failed");
    }

    g_free(d);
}

static void on_update_system_clicked(GtkButton *btn, gpointer user_data)
{
    UpdatesRefreshData *d = (UpdatesRefreshData *)user_data;
    if (g_dry_run) {
        set_status(d->status, "Dry run: would authenticate and open update terminal");
        return;
    }
    /* Confirm with user before starting system update */
    if (!ask_user_yes_no(NULL, "Run system update (pacman -Syu --noconfirm)? This will run as root and make system changes.")) {
        set_status(d->status, "System update cancelled");
        return;
    }

    /* If pacman lock exists, ask to remove it and wait for removal before proceeding */
    if (g_file_test("/var/lib/pacman/db.lck", G_FILE_TEST_EXISTS)) {
        if (!ask_user_yes_no(NULL, "A pacman lock file was detected at /var/lib/pacman/db.lck.\nRemove it and continue?")) {
            set_status(d->status, "Update cancelled: pacman lock present");
            return;
        }

        /* spawn pkexec rm -f and wait for it to complete, then continue to auth/start */
        gchar *argv_rm[] = { "pkexec", "rm", "-f", "/var/lib/pacman/db.lck", NULL };
        GError *err = NULL;
        GPid rm_pid;
        if (!g_spawn_async_with_pipes(NULL, argv_rm, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &rm_pid, NULL, NULL, NULL, &err)) {
            set_status(d->status, "Failed to spawn pkexec to remove lock: %s", err ? err->message : "unknown");
            g_clear_error(&err);
            return;
        }

        /* After removal finishes, continue with auth and start update */
        /* We create a small closure-like struct to hold UpdatesRefreshData */
        UpdatesRefreshData *copy = g_new0(UpdatesRefreshData, 1);
        copy->tv = d->tv;
        copy->status = d->status;
        copy->embedded = d->embedded;
        g_child_watch_add(rm_pid, on_rm_child_done, copy);

        return;
    }

    /* If no lock, prompt for auth first via pkexec /bin/true, then on success on_pkexec_auth_done will start embedded update */
    gchar *argv[] = { "pkexec", "/bin/true", NULL };
    GError *error = NULL;
    GPid child_pid;
    if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &child_pid, NULL, NULL, NULL, &error)) {
        set_status(d->status, "Failed to start pkexec: %s", error ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }
    PKExecAuthData *ad = g_new0(PKExecAuthData, 1);
    ad->status = d->status;
    g_child_watch_add(child_pid, on_pkexec_auth_done, ad);
}

static void on_full_update_clicked(GtkButton *btn, gpointer user_data)
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

    /* Buttons at bottom: Run /bin/update in terminal */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);

    UpdatesRefreshData *d = g_new0(UpdatesRefreshData, 1);
    d->tv = GTK_TEXT_VIEW(tv);
    d->status = status_label;

    GtkWidget *btn_update_system = gtk_button_new_with_label("Update System");
    g_signal_connect(btn_update_system, "clicked", G_CALLBACK(on_update_system_clicked), d);
    gtk_box_append(GTK_BOX(hbox), btn_update_system);

    GtkWidget *btn_full_update = gtk_button_new_with_label("Full Update");
    g_signal_connect(btn_full_update, "clicked", G_CALLBACK(on_full_update_clicked), d);
    gtk_box_append(GTK_BOX(hbox), btn_full_update);

    gtk_box_append(GTK_BOX(vbox), hbox);

    /* Initial population */
    start_refresh_list(GTK_TEXT_VIEW(tv), status_label);

    return vbox;
}
