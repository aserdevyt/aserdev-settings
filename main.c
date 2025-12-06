/*
 * AserDev Settings - GTK4 Settings Application
 * Modular architecture with page modules
 */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <glib.h>

/* Include common helpers */
#include "common.h"

/* Include hyprland module */
#include "hypr.h"

/* Include all page modules */
#include "pages/systeminfo.h"
#include "pages/packages.h"
#include "pages/users.h"
#include "pages/appearance.h"
#include "pages/devices.h"
#include "pages/disks.h"
#include "pages/screenrec.h"
#include "pages/rofi.h"
#include "pages/binds.h"
#include "pages/runcommand.h"
#include "pages/defaultapps.h"
#include "pages/config.h"
#include "pages/audio.h"

/* Sidebar list row selection callback */
static void on_row_selected(GtkListBox *list, GtkListBoxRow *row, GtkStack *stack)
{
    if (!row) return;
    DBG("row-selected called, row=%p", row);
    int index = gtk_list_box_row_get_index(row);
    const char *names[] = {
        "System Info",
        "Software Updates",
        "Users",
        "Appearance",
        "Hyprland",
        "Audio",
        "Devices",
        "Disks",
        "Screen Recording",
        "Binds",
        "Run Command",
        "Default Apps",
        "Config",
        NULL
    };
    if (index >= 0 && names[index]) {
        gtk_stack_set_visible_child_name(stack, names[index]);
    }
}

/* Main application activation */
static void on_activate(GApplication *app, gpointer user_data)
{
    DBG("on_activate called");
    GtkWindow *window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(window, "AserDev Settings");
    gtk_window_set_default_size(window, 900, 520);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_window_set_child(window, hbox);

    /* left sidebar with navigation list */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(left, GTK_ALIGN_START);
    gtk_widget_set_size_request(left, 200, -1);
    gtk_widget_set_hexpand(left, FALSE);

    /* Navigation list */
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);

    GtkWidget *row_sysinfo = gtk_label_new("System Info");
    GtkWidget *row1 = gtk_label_new("Software Updates");
    GtkWidget *row2 = gtk_label_new("Users");
    GtkWidget *row3 = gtk_label_new("Appearance");
    GtkWidget *row_hypr = gtk_label_new("Hyprland");
    GtkWidget *row_audio = gtk_label_new("Audio");
    GtkWidget *row_devices = gtk_label_new("Devices");
    GtkWidget *row_disks = gtk_label_new("Disks");
    /* Clipboard page removed */
    GtkWidget *row5 = gtk_label_new("Screen Recording");
    GtkWidget *row6 = gtk_label_new("Binds");
    GtkWidget *row7 = gtk_label_new("Default Apps");
    GtkWidget *row8 = gtk_label_new("Config");

    gtk_list_box_insert(GTK_LIST_BOX(list), row_sysinfo, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row1, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row2, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row3, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row_hypr, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row_audio, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row_devices, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row_disks, -1);
    /* clipboard row removed */
    gtk_list_box_insert(GTK_LIST_BOX(list), row5, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row6, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row7, -1);
    gtk_list_box_insert(GTK_LIST_BOX(list), row8, -1);
    gtk_box_append(GTK_BOX(left), list);

    gtk_box_append(GTK_BOX(hbox), left);

    /* right: stack + status */
    GtkWidget *right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_set_spacing(GTK_BOX(right_vbox), 6);
    gtk_widget_set_hexpand(right_vbox, TRUE);
    gtk_widget_set_vexpand(right_vbox, TRUE);

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);

    GtkWidget *status_label = gtk_label_new("");
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);

    /* Create all pages */
    GtkWidget *sysinfo_page = create_systeminfo_page();
    GtkWidget *pkg_page = create_packages_page(window, GTK_LABEL(status_label));
    GtkWidget *users_page = create_users_page(window, GTK_LABEL(status_label));
    GtkWidget *appearance_page = create_appearance_page(GTK_LABEL(status_label));
    GtkWidget *hyprland_page = create_hyprland_page(GTK_LABEL(status_label));
    GtkWidget *audio_page = create_audio_page(GTK_LABEL(status_label));
    GtkWidget *devices_page = create_devices_page(GTK_LABEL(status_label));
    GtkWidget *disks_page = create_disks_page(GTK_LABEL(status_label));
    GtkWidget *screenrec_page = create_screenrec_page(GTK_LABEL(status_label));
    GtkWidget *binds_page = create_binds_page(GTK_LABEL(status_label));
    GtkWidget *runcommand_page = create_runcommand_page(GTK_LABEL(status_label));
    GtkWidget *defaultapps_page = create_defaultapps_page(GTK_LABEL(status_label));
    GtkWidget *config_page = create_config_page(GTK_LABEL(status_label));

    /* Add pages to stack */
    gtk_stack_add_named(GTK_STACK(stack), sysinfo_page, "System Info");
    gtk_stack_add_named(GTK_STACK(stack), pkg_page, "Software Updates");
    gtk_stack_add_named(GTK_STACK(stack), users_page, "Users");
    gtk_stack_add_named(GTK_STACK(stack), appearance_page, "Appearance");
    gtk_stack_add_named(GTK_STACK(stack), hyprland_page, "Hyprland");
    gtk_stack_add_named(GTK_STACK(stack), audio_page, "Audio");
    gtk_stack_add_named(GTK_STACK(stack), devices_page, "Devices");
    gtk_stack_add_named(GTK_STACK(stack), disks_page, "Disks");
    /* clipboard page removed from stack */
    gtk_stack_add_named(GTK_STACK(stack), screenrec_page, "Screen Recording");
    gtk_stack_add_named(GTK_STACK(stack), binds_page, "Binds");
    gtk_stack_add_named(GTK_STACK(stack), runcommand_page, "Run Command");
    gtk_stack_add_named(GTK_STACK(stack), defaultapps_page, "Default Apps");
    gtk_stack_add_named(GTK_STACK(stack), config_page, "Config");

    gtk_box_append(GTK_BOX(right_vbox), stack);
    gtk_box_append(GTK_BOX(right_vbox), status_label);
    gtk_box_append(GTK_BOX(hbox), right_vbox);

    g_signal_connect(list, "row-selected", G_CALLBACK(on_row_selected), stack);

    GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0);
    gtk_list_box_select_row(GTK_LIST_BOX(list), first);

    gtk_window_present(window);
    DBG("main window presented");
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;
    
    /* parse --dry-run flag early so UI actions know behavior */
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--dry-run") == 0 || g_strcmp0(argv[i], "-n") == 0) {
            g_dry_run = TRUE;
        }
    }

    app = gtk_application_new("com.aserdev.settings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
