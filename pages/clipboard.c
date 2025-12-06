#include "../common.h"
#include <gtk/gtk.h>

GtkWidget *create_clipboard_page(GtkLabel *status_label)
{
    DBG("create_clipboard_page called");
    (void)status_label;
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *lbl = gtk_label_new("Clipboard support removed");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), lbl);

    return vbox;
}
