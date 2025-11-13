#include "../common.h"
#include <gtk/gtk.h>

/* Adapter: button clicked -> open config dir stored on the button */
static void on_config_button_clicked(GtkButton *btn, gpointer user_data)
{
    const char *subdir = g_object_get_data(G_OBJECT(btn), "config-subdir");
    GtkLabel *status = GTK_LABEL(g_object_get_data(G_OBJECT(btn), "status"));
    if (!subdir) return;
    open_config_dir(subdir, status);
}

/* Local adapters used by this page to call common helpers */
static void config_run_waypaper(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    run_command_and_report("waypaper", GTK_LABEL(user_data));
}

static void config_launch_button(GtkButton *btn, gpointer user_data)
{
    const char *prog = g_object_get_data(G_OBJECT(btn), "prog");
    GtkLabel *status = GTK_LABEL(g_object_get_data(G_OBJECT(btn), "status"));
    if (!prog) return;
    launch_if_found(prog, status);
}

GtkWidget *create_config_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Configuration & Utilities");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Quick access to common config directories and utilities. Open folders in your file manager or run the settings utilities below.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    /* Config folder buttons */
    GtkWidget *h1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_hypr = gtk_button_new_with_label("Open ~/.config/hypr");
    /* store subdir and status on the button so the adapter can call open_config_dir */
    g_object_set_data(G_OBJECT(btn_hypr), "config-subdir", (gpointer)"hypr");
    g_object_set_data(G_OBJECT(btn_hypr), "status", status_label);
    g_signal_connect(btn_hypr, "clicked", G_CALLBACK(on_config_button_clicked), NULL);
    gtk_box_append(GTK_BOX(h1), btn_hypr);
    GtkWidget *btn_waybar_cfg = gtk_button_new_with_label("Open ~/.config/waybar");
    g_object_set_data(G_OBJECT(btn_waybar_cfg), "config-subdir", (gpointer)"waybar");
    g_object_set_data(G_OBJECT(btn_waybar_cfg), "status", status_label);
    g_signal_connect(btn_waybar_cfg, "clicked", G_CALLBACK(on_config_button_clicked), NULL);
    gtk_box_append(GTK_BOX(h1), btn_waybar_cfg);
    GtkWidget *btn_rofi_cfg = gtk_button_new_with_label("Open ~/.config/rofi");
    g_object_set_data(G_OBJECT(btn_rofi_cfg), "config-subdir", (gpointer)"rofi");
    g_object_set_data(G_OBJECT(btn_rofi_cfg), "status", status_label);
    g_signal_connect(btn_rofi_cfg, "clicked", G_CALLBACK(on_config_button_clicked), NULL);
    gtk_box_append(GTK_BOX(h1), btn_rofi_cfg);
    gtk_box_append(GTK_BOX(vbox), h1);

    /* Utilities row */
    GtkWidget *h2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_audio = gtk_button_new_with_label("Audio settings (pavucontrol)");
    gtk_box_append(GTK_BOX(h2), btn_audio);

    GtkWidget *btn_waypaper = gtk_button_new_with_label("Wallpaper (waypaper)");
    /* Use local adapter that calls common helper to run waypaper */
    g_signal_connect(btn_waypaper, "clicked", G_CALLBACK(config_run_waypaper), status_label);
    gtk_box_append(GTK_BOX(h2), btn_waypaper);

    GtkWidget *btn_disks = gtk_button_new_with_label("Disks (gnome-disks)");
    gtk_box_append(GTK_BOX(h2), btn_disks);

    GtkWidget *btn_thunar = gtk_button_new_with_label("File manager settings (thunar-settings)");
    gtk_box_append(GTK_BOX(h2), btn_thunar);

    gtk_box_append(GTK_BOX(vbox), h2);

    /* For buttons that use g_object_get_data to retrieve parameters, wrap their
     * clicked handlers with small adapters that extract the stored data. */
    /* Connect utility buttons to launch the named programs (use adapters) */
    g_object_set_data(G_OBJECT(btn_audio), "prog", (gpointer)"pavucontrol");
    g_object_set_data(G_OBJECT(btn_audio), "status", status_label);
    /* Local adapter to launch program via common helper */
    g_signal_connect(btn_audio, "clicked", G_CALLBACK(config_launch_button), NULL);

    g_object_set_data(G_OBJECT(btn_disks), "prog", (gpointer)"gnome-disks");
    g_object_set_data(G_OBJECT(btn_disks), "status", status_label);
    g_signal_connect(btn_disks, "clicked", G_CALLBACK(config_launch_button), NULL);

    g_object_set_data(G_OBJECT(btn_thunar), "prog", (gpointer)"thunar-settings");
    g_object_set_data(G_OBJECT(btn_thunar), "status", status_label);
    g_signal_connect(btn_thunar, "clicked", G_CALLBACK(config_launch_button), NULL);

    return vbox;
}
