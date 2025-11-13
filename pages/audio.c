#include "../common.h"
#include "audio.h"

/* Static wrapper to call launch_if_found with the provided GtkLabel as status */
static void audio_launch_cb(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    launch_if_found("pavucontrol", status);
}

/* Audio page: simple button to launch pavucontrol if available */
GtkWidget *create_audio_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Audio");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Open your system volume control (pavucontrol) if installed.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    GtkWidget *btn = gtk_button_new_with_label("Open Volume Control (pavucontrol)");
    g_signal_connect(btn, "clicked", G_CALLBACK(audio_launch_cb), status_label);
    gtk_box_append(GTK_BOX(vbox), btn);

    return vbox;
}

/* (no additional helpers) */
