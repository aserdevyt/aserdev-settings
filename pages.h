#ifndef PAGES_H
#define PAGES_H

#include <gtk/gtk.h>

/* System Info Page */
GtkWidget *create_systeminfo_page(void);

/* Packages Page */
GtkWidget *create_packages_page(GtkWindow *parent, GtkLabel *status_label);

/* Users Page */
GtkWidget *create_users_page(GtkWindow *parent, GtkLabel *status_label);

/* Appearance Page */
GtkWidget *create_appearance_page(GtkLabel *status_label);

/* Hyprland Page */
GtkWidget *create_hyprland_page(GtkLabel *status_label);

/* Devices Page */
GtkWidget *create_devices_page(GtkLabel *status_label);

/* Disks Page */
GtkWidget *create_disks_page(GtkLabel *status_label);

/* Clipboard Page */
GtkWidget *create_clipboard_page(GtkLabel *status_label);

/* Screen Recording Page */
GtkWidget *create_screenrec_page(GtkLabel *status_label);

/* Binds Page */
GtkWidget *create_binds_page(GtkLabel *status_label);

/* Default Apps Page */
GtkWidget *create_defaultapps_page(GtkLabel *status_label);

GtkWidget *create_runcommand_page(GtkLabel *status_label);
/* Utilities Page */
GtkWidget *create_config_page(GtkLabel *status_label);
GtkWidget *create_emoji_page(GtkLabel *status_label);
GtkWidget *create_runapp_page(GtkLabel *status_label);

#endif /* PAGES_H */
