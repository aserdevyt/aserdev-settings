#include "../common.h"
#include <gtk/gtk.h>

/* Populate a GtkTextView with lspci -k output and lsusb output (if available). */
static void populate_devices_text(GtkTextView *tv)
{
    if (!tv) return;
    GString *outbuf = g_string_new(NULL);

    gchar *out = NULL;
    gchar *err = NULL;
    gint exit_status = 0;
    GError *gerr = NULL;

    /* lspci -k (show kernel drivers) */
    gboolean ok = g_spawn_command_line_sync("lspci -k", &out, &err, &exit_status, &gerr);
    if (ok && out && *out) {
        g_string_append(outbuf, "lspci -k output:\n");
        g_string_append(outbuf, out);
        g_string_append(outbuf, "\n");
    } else {
        g_string_append(outbuf, "lspci not available or failed to run.\n");
        if (gerr) {
            g_string_append_printf(outbuf, "error: %s\n\n", gerr->message);
            g_clear_error(&gerr);
        } else if (err && *err) {
            g_string_append(outbuf, err);
            g_string_append(outbuf, "\n");
        }
    }
    g_free(out);
    g_free(err);

    /* lsusb (optional) */
    char *path_lsusb = g_find_program_in_path("lsusb");
    if (path_lsusb) {
        g_free(path_lsusb);
        out = NULL; err = NULL; exit_status = 0; gerr = NULL;
        ok = g_spawn_command_line_sync("lsusb", &out, &err, &exit_status, &gerr);
        if (ok && out && *out) {
            g_string_append(outbuf, "lsusb output:\n");
            g_string_append(outbuf, out);
            g_string_append(outbuf, "\n");
        } else {
            g_string_append(outbuf, "lsusb failed to run.\n");
            if (gerr) { g_string_append_printf(outbuf, "error: %s\n\n", gerr->message); g_clear_error(&gerr); }
            else if (err && *err) { g_string_append(outbuf, err); g_string_append(outbuf, "\n"); }
        }
        g_free(out);
        g_free(err);
    } else {
        g_string_append(outbuf, "lsusb not found (skipping)\n");
    }

    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    gtk_text_buffer_set_text(buf, outbuf->str, -1);
    g_string_free(outbuf, TRUE);
}

static void on_devices_refresh_clicked(GtkButton *btn, gpointer user_data)
{
    GtkTextView *tv = GTK_TEXT_VIEW(user_data);
    populate_devices_text(tv);
}

GtkWidget *create_devices_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Devices");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("System devices: lspci -k and lsusb (if available). Use Refresh to update.");
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
    populate_devices_text(GTK_TEXT_VIEW(tv));

    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_devices_refresh_clicked), tv);
    gtk_box_append(GTK_BOX(h), btn_refresh);
    gtk_widget_set_halign(h, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), h);

    return vbox;
}
