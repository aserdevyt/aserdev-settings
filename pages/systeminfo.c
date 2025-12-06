#include "../common.h"
#include <gtk/gtk.h>

GtkWidget *create_systeminfo_page(void)
{
    DBG("create_systeminfo_page called");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroller);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='large' weight='bold'>System Information</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(content), title);

    /* Gather system info via common helpers */
    gchar *os_name = get_os_name();
    gchar *hostname = get_system_info_item("uname -n");
    gchar *kernel = get_system_info_item("uname -r");
    gchar *arch = get_system_info_item("uname -m");
    gchar *cpu_name = get_cpu_name();
    gchar *ram_info = get_ram_info();
    gchar *uptime = get_system_info_item("uptime -p");
    gchar *cpu_cores = get_system_info_item("nproc");
    gchar *disk_usage = get_disk_usage("/");
    gchar *whoami = get_system_info_item("whoami");

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
    gtk_box_append(GTK_BOX(content), grid);

    int row = 0;

    GtkWidget *label_os = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_os), "<b>Operating System</b>");
    gtk_widget_set_halign(label_os, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_os, 0, row, 1, 1);
    GtkWidget *value_os = gtk_label_new(os_name);
    gtk_label_set_wrap(GTK_LABEL(value_os), TRUE);
    gtk_widget_set_halign(value_os, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_os, 1, row, 1, 1);
    row++;

    GtkWidget *label_hostname = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_hostname), "<b>Hostname</b>");
    gtk_widget_set_halign(label_hostname, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_hostname, 0, row, 1, 1);
    GtkWidget *value_hostname = gtk_label_new(hostname);
    gtk_label_set_wrap(GTK_LABEL(value_hostname), TRUE);
    gtk_widget_set_halign(value_hostname, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_hostname, 1, row, 1, 1);
    row++;

    GtkWidget *label_user = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_user), "<b>Current User</b>");
    gtk_widget_set_halign(label_user, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_user, 0, row, 1, 1);
    GtkWidget *value_user = gtk_label_new(whoami);
    gtk_label_set_wrap(GTK_LABEL(value_user), TRUE);
    gtk_widget_set_halign(value_user, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_user, 1, row, 1, 1);
    row++;

    GtkWidget *label_kernel = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_kernel), "<b>Kernel Version</b>");
    gtk_widget_set_halign(label_kernel, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_kernel, 0, row, 1, 1);
    GtkWidget *value_kernel = gtk_label_new(kernel);
    gtk_label_set_wrap(GTK_LABEL(value_kernel), TRUE);
    gtk_widget_set_halign(value_kernel, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_kernel, 1, row, 1, 1);
    row++;

    GtkWidget *label_arch = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_arch), "<b>CPU Architecture</b>");
    gtk_widget_set_halign(label_arch, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_arch, 0, row, 1, 1);
    GtkWidget *value_arch = gtk_label_new(arch);
    gtk_label_set_wrap(GTK_LABEL(value_arch), TRUE);
    gtk_widget_set_halign(value_arch, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_arch, 1, row, 1, 1);
    row++;

    GtkWidget *label_cpu = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_cpu), "<b>CPU</b>");
    gtk_widget_set_halign(label_cpu, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_cpu, 0, row, 1, 1);
    GtkWidget *value_cpu = gtk_label_new(cpu_name);
    gtk_label_set_wrap(GTK_LABEL(value_cpu), TRUE);
    gtk_widget_set_halign(value_cpu, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_cpu, 1, row, 1, 1);
    row++;

    GtkWidget *label_cores = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_cores), "<b>CPU Cores</b>");
    gtk_widget_set_halign(label_cores, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_cores, 0, row, 1, 1);
    GtkWidget *value_cores = gtk_label_new(cpu_cores);
    gtk_label_set_wrap(GTK_LABEL(value_cores), TRUE);
    gtk_widget_set_halign(value_cores, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_cores, 1, row, 1, 1);
    row++;

    GtkWidget *label_ram = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_ram), "<b>Total RAM</b>");
    gtk_widget_set_halign(label_ram, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_ram, 0, row, 1, 1);
    GtkWidget *value_ram = gtk_label_new(ram_info);
    gtk_label_set_wrap(GTK_LABEL(value_ram), TRUE);
    gtk_widget_set_halign(value_ram, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_ram, 1, row, 1, 1);
    row++;

    GtkWidget *label_disk = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_disk), "<b>Root Disk Usage</b>");
    gtk_widget_set_halign(label_disk, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_disk, 0, row, 1, 1);
    GtkWidget *value_disk = gtk_label_new(disk_usage);
    gtk_label_set_wrap(GTK_LABEL(value_disk), TRUE);
    gtk_widget_set_halign(value_disk, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_disk, 1, row, 1, 1);
    row++;

    GtkWidget *label_uptime = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label_uptime), "<b>System Uptime</b>");
    gtk_widget_set_halign(label_uptime, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label_uptime, 0, row, 1, 1);
    GtkWidget *value_uptime = gtk_label_new(uptime);
    gtk_label_set_wrap(GTK_LABEL(value_uptime), TRUE);
    gtk_widget_set_halign(value_uptime, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), value_uptime, 1, row, 1, 1);

    g_free(os_name);
    g_free(hostname);
    g_free(kernel);
    g_free(arch);
    g_free(cpu_name);
    g_free(ram_info);
    g_free(uptime);
    g_free(cpu_cores);
    g_free(disk_usage);
    g_free(whoami);

    return vbox;
}
