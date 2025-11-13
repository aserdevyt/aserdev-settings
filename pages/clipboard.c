#include "../common.h"
#include <gtk/gtk.h>

/* Clipboard callbacks */
static void on_clipboard_open(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    run_command_and_report("rofi-cliphist", status);
}

static void on_clipboard_wipe(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    run_command_and_report("cliphist wipe", status);
}

GtkWidget *create_clipboard_page(GtkLabel *status_label)
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
