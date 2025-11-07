/* hypr.h - Hyprland config editor public header */
#ifndef HYPR_H
#define HYPR_H

#include <gtk/gtk.h>

/* A convenient exported status helper prototype (implemented in main.c) */
void set_status(GtkLabel *status, const char *fmt, ...);

/* Show a big modal dialog with title and text (for errors/output). */
void show_big_message_dialog(const char *title, const char *text);

/* Create the Hyprland page (large text editor) */
GtkWidget *create_hyprland_page(GtkLabel *status_label);

#endif /* HYPR_H */
