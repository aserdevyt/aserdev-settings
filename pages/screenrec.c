#include "../common.h"
#include <gtk/gtk.h>

/* Screen recording */
static void on_screenrec_run(GtkButton *btn, gpointer user_data)
{
    DBG("on_screenrec_run called");
    GtkLabel *status = GTK_LABEL(user_data);
    char *path = g_find_program_in_path("screenrec");
    if (path) {
        run_command_and_report(path, status);
        g_free(path);
    } else {
        set_status(status, "screenrec not found in PATH");
    }
}

GtkWidget *create_screenrec_page(GtkLabel *status_label)
{
    DBG("create_screenrec_page called");
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
