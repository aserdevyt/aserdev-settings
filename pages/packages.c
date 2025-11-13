#include "../common.h"
#include <gtk/gtk.h>

typedef struct {
    GtkWindow  *parent;
    GtkEntry   *entry;
    GtkLabel   *status_label;
} PackageActionData;

static void on_package_action(GtkButton *button, gpointer user_data)
{
    PackageActionData *d = (PackageActionData *)user_data;
    const char *action = (const char *)g_object_get_data(G_OBJECT(button), "action");
    const char *pkg = gtk_editable_get_text(GTK_EDITABLE(d->entry));

    if (!pkg || strlen(pkg) == 0) {
        set_status(d->status_label, "Please enter a package name.");
        return;
    }

    gboolean execute = !g_dry_run; /* execute unless running in --dry-run mode */
    char *cmd = NULL;

    if (g_strcmp0(action, "aur") == 0) {
        char *prefix = get_terminal_prefix();
        char *quoted = g_shell_quote(pkg);
        cmd = g_strdup_printf(prefix, g_strdup_printf("yay -S --needed --noconfirm %s", quoted));
        g_free(prefix);
        g_free(quoted);
    } else if (g_strcmp0(action, "pacman") == 0) {
        char *prefix = get_terminal_prefix();
        char *quoted = g_shell_quote(pkg);
        cmd = g_strdup_printf(prefix, g_strdup_printf("sudo pacman -S --noconfirm %s", quoted));
        g_free(prefix);
        g_free(quoted);
    } else if (g_strcmp0(action, "flatpak") == 0) {
        char *prefix = get_terminal_prefix();
        char *quoted = g_shell_quote(pkg);
        cmd = g_strdup_printf(prefix, g_strdup_printf("flatpak install -y flathub %s", quoted));
        g_free(prefix);
        g_free(quoted);
    } else if (g_strcmp0(action, "update_aur_flatpak") == 0) {
        char *prefix = get_terminal_prefix();
        cmd = g_strdup_printf(prefix, "yay -Syu && flatpak update -y");
        g_free(prefix);
    } else {
        cmd = g_strdup("echo unknown-action");
    }

    if (!execute) {
        set_status(d->status_label, "Dry run: %s", cmd);
        g_free(cmd);
        return;
    }

    run_command_and_report(cmd, d->status_label);
    g_free(cmd);
}

GtkWidget *create_packages_page(GtkWindow *parent, GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Package name:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "e.g. firefox");
    gtk_box_append(GTK_BOX(vbox), entry);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_box_append(GTK_BOX(vbox), grid);

    const char *btn_labels[] = {"Install (AUR)", "Install (Pacman)", "Install (Flatpak)", "Update (AUR + Flatpak)"};
    const char *actions[] = {"aur", "pacman", "flatpak", "update_aur_flatpak"};

    PackageActionData *pdata = g_new0(PackageActionData, 1);
    pdata->parent = parent;
    pdata->entry = GTK_ENTRY(entry);
    pdata->status_label = status_label;

    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(btn_labels[i]);
        g_object_set_data_full(G_OBJECT(btn), "action", g_strdup(actions[i]), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_package_action), pdata);
        gtk_grid_attach(GTK_GRID(grid), btn, i % 2, i / 2, 1, 1);
    }

    return vbox;
}
