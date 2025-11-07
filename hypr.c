/* hypr.c - Hyprland configuration editor and large message dialog */
#include "hypr.h"
#include <glib.h>
#include <glib/gstdio.h>

/* Show a large modal error/info dialog with the provided text. Caller may
 * pass NULL for title to use a default. This is used for save errors or
 * long output messages that need a big, scrollable view. */
void show_big_message_dialog(const char *title, const char *text)
{
    GtkWidget *dlg = gtk_window_new();
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 900, 600);
    if (title) gtk_window_set_title(GTK_WINDOW(dlg), title);

    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(v, 8);
    gtk_widget_set_margin_end(v, 8);
    gtk_widget_set_margin_top(v, 8);
    gtk_widget_set_margin_bottom(v, 8);
    gtk_window_set_child(GTK_WINDOW(dlg), v);

    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sc, TRUE);
    gtk_widget_set_hexpand(sc, TRUE);
    gtk_box_append(GTK_BOX(v), sc);

    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    if (text && *text) gtk_text_buffer_set_text(buf, text, -1);
    else gtk_text_buffer_set_text(buf, "(no output)", -1);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), tv);

    GtkWidget *btn = gtk_button_new_with_label("Close");
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_window_destroy), dlg);
    gtk_box_append(GTK_BOX(v), btn);

    gtk_window_present(GTK_WINDOW(dlg));
}

/* Data for hyprland save callback */
typedef struct {
    GtkTextView *tv;
    char *path;
    GtkLabel *status;
} HyprlandSaveData;

static void on_hyprland_save_clicked(GtkButton *btn, gpointer user_data)
{
    HyprlandSaveData *d = (HyprlandSaveData *)user_data;
    if (!d) return;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(d->tv);
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buf, &start);
    gtk_text_buffer_get_end_iter(buf, &end);
    char *txt = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    GError *err = NULL;
    if (!g_file_set_contents(d->path, txt, -1, &err)) {
        char *msg = g_strdup_printf("Failed to write %s:\n%s", d->path, err ? err->message : "unknown");
        show_big_message_dialog("Error saving hyprland.conf", msg);
        g_free(msg);
        if (err) g_clear_error(&err);
    } else {
        set_status(d->status, "Saved %s", d->path);
    }
    g_free(txt);
}

GtkWidget *create_hyprland_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Hyprland Configuration");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Edit your ~/.config/hypr/hyprland.conf below. Use Save to write changes. Errors/output will be shown in a large dialog.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroller);

    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), tv);

    /* load file contents */
    char *path = g_build_filename(g_get_home_dir(), ".config", "hypr", "hyprland.conf", NULL);
    gchar *content = NULL;
    if (g_file_get_contents(path, &content, NULL, NULL)) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
        gtk_text_buffer_set_text(buf, content, -1);
        g_free(content);
    } else {
        /* leave empty and inform status */
        set_status(status_label, "Could not read %s (it may not exist)", path);
    }

    /* Save button */
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_save = gtk_button_new_with_label("Save hyprland.conf");
    gtk_box_append(GTK_BOX(h), btn_save);
    gtk_widget_set_halign(h, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), h);

    HyprlandSaveData *sd = g_new0(HyprlandSaveData, 1);
    sd->tv = GTK_TEXT_VIEW(tv);
    sd->path = path;
    sd->status = status_label;
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_hyprland_save_clicked), sd);

    return vbox;
}
