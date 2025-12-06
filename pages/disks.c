#include "../common.h"
#include <gtk/gtk.h>

/* Populate a GtkTextView with lsblk output */
static void populate_disks_text(GtkTextView *tv)
{
    DBG("populate_disks_text called");
    if (!tv) return;
    GString *outbuf = g_string_new(NULL);

    gchar *out = NULL;
    gchar *err = NULL;
    gint exit_status = 0;
    GError *gerr = NULL;

    /* lsblk - block device info with more details */
    gboolean ok = g_spawn_command_line_sync("lsblk -lpo NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT", &out, &err, &exit_status, &gerr);
    if (ok && out && *out) {
        g_string_append(outbuf, "Block Devices (lsblk):\n");
        g_string_append(outbuf, out);
        g_string_append(outbuf, "\n\n");
    } else {
        g_string_append(outbuf, "lsblk not available or failed to run.\n");
        if (gerr) {
            g_string_append_printf(outbuf, "error: %s\n\n", gerr->message);
            g_clear_error(&gerr);
        } else if (err && *err) {
            g_string_append(outbuf, err);
            g_string_append(outbuf, "\n\n");
        }
    }
    g_free(out);
    g_free(err);

    /* df - filesystem usage info */
    out = NULL; err = NULL; exit_status = 0; gerr = NULL;
    ok = g_spawn_command_line_sync("df -h", &out, &err, &exit_status, &gerr);
    if (ok && out && *out) {
        g_string_append(outbuf, "Filesystem Usage (df -h):\n");
        g_string_append(outbuf, out);
        g_string_append(outbuf, "\n\n");
    } else {
        g_string_append(outbuf, "df not available or failed to run.\n");
        if (gerr) { g_string_append_printf(outbuf, "error: %s\n\n", gerr->message); g_clear_error(&gerr); }
        else if (err && *err) { g_string_append(outbuf, err); g_string_append(outbuf, "\n\n"); }
    }
    g_free(out);
    g_free(err);

    /* du - disk usage of home directory */
    out = NULL; err = NULL; exit_status = 0; gerr = NULL;
    char *home_cmd = g_strdup_printf("du -sh %s", g_get_home_dir());
    ok = g_spawn_command_line_sync(home_cmd, &out, &err, &exit_status, &gerr);
    if (ok && out && *out) {
        g_string_append(outbuf, "Home Directory Size:\n");
        g_string_append(outbuf, out);
        g_string_append(outbuf, "\n\n");
    }
    g_free(home_cmd);
    g_free(out);
    g_free(err);

    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    gtk_text_buffer_set_text(buf, outbuf->str, -1);
    g_string_free(outbuf, TRUE);
}

static void on_disks_refresh_clicked(GtkButton *btn, gpointer user_data)
{
    DBG("on_disks_refresh_clicked called");
    GtkTextView *tv = GTK_TEXT_VIEW(user_data);
    populate_disks_text(tv);
}

static void on_gnome_disks_clicked(GtkButton *btn, gpointer user_data)
{
    GError *gerr = NULL;
    gboolean ok = g_spawn_command_line_async("gnome-disks", &gerr);
    if (!ok) {
        if (gerr) {
            g_warning("Failed to launch gnome-disks: %s", gerr->message);
            g_clear_error(&gerr);
        }
    }
}

GtkWidget *create_disks_page(GtkLabel *status_label)
{
    DBG("create_disks_page called");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Disks");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Block devices, filesystem usage, and home directory size.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroller);

    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), tv);

    /* initial populate */
    populate_disks_text(GTK_TEXT_VIEW(tv));

    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_disks_refresh_clicked), tv);
    gtk_box_append(GTK_BOX(h), btn_refresh);

    GtkWidget *btn_gnome_disks = gtk_button_new_with_label("Open Gnome Disks");
    g_signal_connect(btn_gnome_disks, "clicked", G_CALLBACK(on_gnome_disks_clicked), NULL);
    gtk_box_append(GTK_BOX(h), btn_gnome_disks);

    gtk_widget_set_halign(h, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), h);

    return vbox;
}
