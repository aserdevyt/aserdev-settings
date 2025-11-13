#include "../common.h"
#include <gtk/gtk.h>

/* Emoji and rofi */
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

GtkWidget *create_emoji_page(GtkLabel *status_label)
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

GtkWidget *create_runapp_page(GtkLabel *status_label)
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
