#include "../common.h"
#include <gtk/gtk.h>

/* Run Command page: ask for command and optionally run as root inside a terminal */
static void on_run_command_clicked(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkEntry *entry = GTK_ENTRY(ud[0]);
    GtkCheckButton *chk_root = GTK_CHECK_BUTTON(ud[1]);
    GtkCheckButton *chk_terminal = GTK_CHECK_BUTTON(ud[2]);
    GtkLabel *status = GTK_LABEL(ud[3]);

    const char *cmdtext = gtk_editable_get_text(GTK_EDITABLE(entry));
    gboolean as_root = gtk_check_button_get_active(chk_root);
    gboolean run_in_terminal = gtk_check_button_get_active(chk_terminal);

    if (!cmdtext || strlen(cmdtext) == 0) {
        set_status(status, "Please enter a command to run");
        return;
    }

    /* If user chose to run inside a terminal, open the preferred terminal
     * emulator and run via bash -lc (optionally prefixed with sudo). */
    if (run_in_terminal) {
        char *quoted = g_shell_quote(cmdtext);
        char *inner = NULL;
        if (as_root) {
            inner = g_strdup_printf("sudo bash -lc %s", quoted);
        } else {
            inner = g_strdup_printf("bash -lc %s", quoted);
        }
        char *prefix = get_terminal_prefix();
        char *cmd = g_strdup_printf(prefix, inner);
        run_command_and_report(cmd, status);
        g_free(quoted);
        g_free(inner);
        g_free(prefix);
        g_free(cmd);
        return;
    }

    /* Non-interactive mode: if running as root, use pkexec and pass the
     * command via stdin to /bin/bash -s; otherwise run bash -lc directly. */
    if (as_root) {
        run_command_via_pkexec_stdin(cmdtext, status);
    } else {
        /* run non-interactively via bash -lc */
        char *quoted = g_shell_quote(cmdtext);
        char *cmd = g_strdup_printf("bash -lc %s", quoted);
        run_command_and_report(cmd, status);
        g_free(quoted);
        g_free(cmd);
    }
}

GtkWidget *create_runcommand_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Run command");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Command to run, e.g. top");
    gtk_box_append(GTK_BOX(vbox), entry);

    GtkWidget *chk_root = gtk_check_button_new_with_label("Run as root (sudo)");
    gtk_box_append(GTK_BOX(vbox), chk_root);

    GtkWidget *chk_terminal = gtk_check_button_new_with_label("Run in terminal");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(chk_terminal), TRUE);
    gtk_box_append(GTK_BOX(vbox), chk_terminal);

    GtkWidget *btn = gtk_button_new_with_label("Run");
    gpointer *ud = g_new(gpointer, 4);
    ud[0] = entry;
    ud[1] = chk_root;
    ud[2] = chk_terminal;
    ud[3] = status_label;
    g_signal_connect(btn, "clicked", G_CALLBACK(on_run_command_clicked), ud);
    gtk_box_append(GTK_BOX(vbox), btn);

    return vbox;
}
