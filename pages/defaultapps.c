#include "../common.h"
#include <gtk/gtk.h>

/* Helper: query xdg-mime for the current default of a given mime/handler.
 * Returns a newly-allocated string (caller must g_free) or NULL on failure. */
static char *query_xdg_mime_default(const char *mime)
{
    gchar *out = NULL;
    gchar *err = NULL;
    gint exit_status = 0;
    GError *error = NULL;
    char *cmd = g_strdup_printf("xdg-mime query default %s", mime);
    gboolean ok = g_spawn_command_line_sync(cmd, &out, &err, &exit_status, &error);
    g_free(cmd);

    if (!ok) {
        if (error) g_clear_error(&error);
        g_free(out);
        g_free(err);
        return NULL;
    }

    if (!out) {
        g_free(err);
        return NULL;
    }

    g_strchomp(out);
    char *res = g_strdup(out);
    g_free(out);
    g_free(err);
    return res;
}

static void on_default_apps_apply(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkEntry *entry_term = GTK_ENTRY(ud[0]);
    GtkEntry *entry_fm = GTK_ENTRY(ud[1]);
    GtkEntry *entry_browser = GTK_ENTRY(ud[2]);
    GtkEntry *entry_editor = GTK_ENTRY(ud[3]);
    GtkLabel *status = GTK_LABEL(ud[4]);
    GtkLabel *cur_term = GTK_LABEL(ud[5]);
    GtkLabel *cur_fm = GTK_LABEL(ud[6]);
    GtkLabel *cur_browser = GTK_LABEL(ud[7]);
    GtkLabel *cur_editor = GTK_LABEL(ud[8]);

    const char *term = gtk_editable_get_text(GTK_EDITABLE(entry_term));
    const char *fm = gtk_editable_get_text(GTK_EDITABLE(entry_fm));
    const char *browser = gtk_editable_get_text(GTK_EDITABLE(entry_browser));
    const char *editor = gtk_editable_get_text(GTK_EDITABLE(entry_editor));

    if (term && strlen(term) > 0) {
        char *q = g_shell_quote(term);
        char *cmd = g_strdup_printf("xdg-mime default %s x-scheme-handler/terminal", q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
    }
    if (fm && strlen(fm) > 0) {
        char *q = g_shell_quote(fm);
        char *cmd = g_strdup_printf("xdg-mime default %s inode/directory", q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
    }
    if (browser && strlen(browser) > 0) {
        char *q = g_shell_quote(browser);
        /* Use xdg-mime for HTTP handler so we get a desktop file name */
        char *cmd = g_strdup_printf("xdg-mime default %s x-scheme-handler/http", q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
    }
    if (editor && strlen(editor) > 0) {
        char *q = g_shell_quote(editor);
        char *cmd = g_strdup_printf("xdg-mime default %s text/plain", q);
        run_command_and_report(cmd, status);
        g_free(q);
        g_free(cmd);
    }

    set_status(status, "Default app commands launched (check status)");

    /* Refresh the visible current-default labels */
    if (cur_term) {
        char *val = NULL;
        val = g_strdup(query_xdg_mime_default("x-scheme-handler/terminal"));
        if (val) {
            gtk_label_set_text(cur_term, val);
            g_free(val);
        } else {
            gtk_label_set_text(cur_term, "(unknown)");
        }
    }
    if (cur_fm) {
        char *val = NULL;
        val = g_strdup(query_xdg_mime_default("inode/directory"));
        if (val) {
            gtk_label_set_text(cur_fm, val);
            g_free(val);
        } else {
            gtk_label_set_text(cur_fm, "(unknown)");
        }
    }
    if (cur_browser) {
        char *val = NULL;
        val = g_strdup(query_xdg_mime_default("x-scheme-handler/http"));
        if (val) {
            gtk_label_set_text(cur_browser, val);
            g_free(val);
        } else {
            gtk_label_set_text(cur_browser, "(unknown)");
        }
    }
    if (cur_editor) {
        char *val = NULL;
        val = g_strdup(query_xdg_mime_default("text/plain"));
        if (val) {
            gtk_label_set_text(cur_editor, val);
            g_free(val);
        } else {
            gtk_label_set_text(cur_editor, "(unknown)");
        }
    }
}

/* File-chooser helpers for selecting .desktop files and putting the basename
 * into a GtkEntry. */
static void on_defaultapp_filechosen(GtkNativeDialog *dialog, int response, gpointer user_data)
{
    GtkEntry *entry = GTK_ENTRY(user_data);
    if (response == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GFile *file = gtk_file_chooser_get_file(chooser);
        if (file) {
            char *path = g_file_get_path(file);
            if (path) {
                char *base = g_path_get_basename(path);
                gtk_editable_set_text(GTK_EDITABLE(entry), base);
                g_free(base);
                g_free(path);
            }
            g_object_unref(file);
        }
    }
    gtk_native_dialog_destroy(dialog);
}

static void on_choose_desktop_clicked(GtkButton *btn, gpointer user_data)
{
    GtkEntry *entry = GTK_ENTRY(user_data);
    GtkFileChooserNative *fc = gtk_file_chooser_native_new("Choose .desktop file", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, "Select", "Cancel");
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(fc), FALSE);
    g_signal_connect(fc, "response", G_CALLBACK(on_defaultapp_filechosen), entry);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(fc));
}

GtkWidget *create_defaultapps_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Default Applications");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Choose the .desktop file name that should be used as the default for each role. Use the Choose button to browse for a .desktop file. Current defaults are shown on the right.");
    gtk_widget_set_hexpand(desc, TRUE);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(vbox), grid);

    /* Terminal */
    GtkWidget *lbl_term = gtk_label_new("Terminal:");
    gtk_widget_set_halign(lbl_term, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl_term, 0, 0, 1, 1);
    GtkWidget *entry_term = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_term), "e.g. org.gnome.Terminal.desktop");
    gtk_widget_set_hexpand(entry_term, TRUE);
    gtk_widget_set_size_request(entry_term, 520, -1);
    gtk_grid_attach(GTK_GRID(grid), entry_term, 1, 0, 1, 1);
    GtkWidget *btn_term = gtk_button_new_with_label("Choose");
    gtk_grid_attach(GTK_GRID(grid), btn_term, 2, 0, 1, 1);
    GtkWidget *cur_term = gtk_label_new("(unknown)");
    gtk_widget_set_halign(cur_term, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), cur_term, 3, 0, 1, 1);

    /* File manager */
    GtkWidget *lbl_fm = gtk_label_new("File manager:");
    gtk_widget_set_halign(lbl_fm, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl_fm, 0, 1, 1, 1);
    GtkWidget *entry_fm = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_fm), "e.g. org.gnome.Nautilus.desktop");
    gtk_widget_set_hexpand(entry_fm, TRUE);
    gtk_widget_set_size_request(entry_fm, 520, -1);
    gtk_grid_attach(GTK_GRID(grid), entry_fm, 1, 1, 1, 1);
    GtkWidget *btn_fm = gtk_button_new_with_label("Choose");
    gtk_grid_attach(GTK_GRID(grid), btn_fm, 2, 1, 1, 1);
    GtkWidget *cur_fm = gtk_label_new("(unknown)");
    gtk_widget_set_halign(cur_fm, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), cur_fm, 3, 1, 1, 1);

    /* Browser */
    GtkWidget *lbl_browser = gtk_label_new("Web browser:");
    gtk_widget_set_halign(lbl_browser, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl_browser, 0, 2, 1, 1);
    GtkWidget *entry_browser = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_browser), "e.g. firefox.desktop");
    gtk_widget_set_hexpand(entry_browser, TRUE);
    gtk_widget_set_size_request(entry_browser, 520, -1);
    gtk_grid_attach(GTK_GRID(grid), entry_browser, 1, 2, 1, 1);
    GtkWidget *btn_browser = gtk_button_new_with_label("Choose");
    gtk_grid_attach(GTK_GRID(grid), btn_browser, 2, 2, 1, 1);
    GtkWidget *cur_browser = gtk_label_new("(unknown)");
    gtk_widget_set_halign(cur_browser, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), cur_browser, 3, 2, 1, 1);

    /* Text editor */
    GtkWidget *lbl_editor = gtk_label_new("Text editor:");
    gtk_widget_set_halign(lbl_editor, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), lbl_editor, 0, 3, 1, 1);
    GtkWidget *entry_editor = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_editor), "e.g. org.gnome.gedit.desktop");
    gtk_widget_set_hexpand(entry_editor, TRUE);
    gtk_widget_set_size_request(entry_editor, 520, -1);
    gtk_grid_attach(GTK_GRID(grid), entry_editor, 1, 3, 1, 1);
    GtkWidget *btn_editor = gtk_button_new_with_label("Choose");
    gtk_grid_attach(GTK_GRID(grid), btn_editor, 2, 3, 1, 1);
    GtkWidget *cur_editor = gtk_label_new("(unknown)");
    gtk_widget_set_halign(cur_editor, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), cur_editor, 3, 3, 1, 1);

    GtkWidget *btn = gtk_button_new_with_label("Apply defaults");
    gpointer *ud = g_new(gpointer, 9);
    ud[0] = entry_term;
    ud[1] = entry_fm;
    ud[2] = entry_browser;
    ud[3] = entry_editor;
    ud[4] = status_label;
    ud[5] = cur_term;
    ud[6] = cur_fm;
    ud[7] = cur_browser;
    ud[8] = cur_editor;
    g_signal_connect(btn, "clicked", G_CALLBACK(on_default_apps_apply), ud);
    gtk_box_append(GTK_BOX(vbox), btn);

    /* file-chooser callbacks: each needs the corresponding entry as user_data */
    g_signal_connect(btn_term, "clicked", G_CALLBACK(on_choose_desktop_clicked), entry_term);
    g_signal_connect(btn_fm, "clicked", G_CALLBACK(on_choose_desktop_clicked), entry_fm);
    g_signal_connect(btn_browser, "clicked", G_CALLBACK(on_choose_desktop_clicked), entry_browser);
    g_signal_connect(btn_editor, "clicked", G_CALLBACK(on_choose_desktop_clicked), entry_editor);

    /* initialize current-default labels from xdg */
    char *v = query_xdg_mime_default("x-scheme-handler/terminal");
    if (v) { gtk_label_set_text(GTK_LABEL(cur_term), v); g_free(v); }
    v = query_xdg_mime_default("inode/directory");
    if (v) { gtk_label_set_text(GTK_LABEL(cur_fm), v); g_free(v); }
    v = query_xdg_mime_default("x-scheme-handler/http");
    if (v) { gtk_label_set_text(GTK_LABEL(cur_browser), v); g_free(v); }
    v = query_xdg_mime_default("text/plain");
    if (v) { gtk_label_set_text(GTK_LABEL(cur_editor), v); g_free(v); }

    return vbox;
}
