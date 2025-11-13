#include "../common.h"
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>

/* Appearance page: wallpaper and theme setters */
typedef struct {
    GtkComboBoxText *cb;
    GtkEntry *entry;
    GtkLabel *status;
    const char *setting_name;
} ThemeSetData;

static gboolean apply_gtk_settings(const char *setting_name, const char *value, GError **error)
{
    const char *dirs[] = { "gtk-3.0", "gtk-4.0", NULL };
    gboolean success = TRUE;

    for (int i = 0; dirs[i]; i++) {
        gchar *dir_path = g_build_filename(g_get_home_dir(), ".config", dirs[i], NULL);
        g_mkdir_with_parents(dir_path, 0700);

        gchar *settings_path = g_build_filename(dir_path, "settings.ini", NULL);

        gchar *contents = NULL;
        gsize len = 0;
        GError *read_err = NULL;
        gboolean file_exists = g_file_get_contents(settings_path, &contents, &len, &read_err);
        if (!file_exists && read_err && read_err->code != G_FILE_ERROR_NOENT) {
            if (error) g_propagate_error(error, read_err);
            success = FALSE;
            g_free(dir_path);
            g_free(settings_path);
            continue;
        }
        g_clear_error(&read_err);

        GString *new_contents = g_string_new(NULL);
        gboolean found = FALSE;

        if (contents && *contents) {
            gchar **lines = g_strsplit(contents, "\n", -1);
            for (int j = 0; lines[j]; j++) {
                gchar *line = lines[j];
                gchar *stripped = g_strdup(line);
                g_strstrip(stripped);

                gchar *key_pattern = g_strdup_printf("%s=", setting_name);
                if (g_str_has_prefix(stripped, key_pattern)) {
                    g_string_append_printf(new_contents, "%s=%s\n", setting_name, value);
                    found = TRUE;
                } else if (*stripped && !g_str_has_prefix(stripped, "#")) {
                    g_string_append(new_contents, line);
                    if (lines[j + 1]) g_string_append(new_contents, "\n");
                } else if (*stripped) {
                    g_string_append(new_contents, line);
                    if (lines[j + 1]) g_string_append(new_contents, "\n");
                }
                g_free(stripped);
                g_free(key_pattern);
            }
            g_strfreev(lines);
        }

        if (!found) {
            if (new_contents->len > 0 && !g_str_has_suffix(new_contents->str, "\n")) {
                g_string_append(new_contents, "\n");
            }
            if (new_contents->len == 0 || !g_strrstr(new_contents->str, "[Settings]")) {
                g_string_append(new_contents, "[Settings]\n");
            }
            g_string_append_printf(new_contents, "%s=%s\n", setting_name, value);
        }

        GError *write_err = NULL;
        gboolean write_ok = g_file_set_contents(settings_path, new_contents->str, -1, &write_err);
        if (!write_ok) {
            if (error) g_propagate_error(error, write_err);
            success = FALSE;
        }
        g_free(contents);
        g_string_free(new_contents, TRUE);
        g_free(dir_path);
        g_free(settings_path);
    }

    return success;
}

static void on_run_waypaper(GtkButton *button, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    const char *cmd = "waypaper";
    run_command_and_report(cmd, status);
}

static void on_set_theme_clicked(GtkButton *btn, gpointer user_data)
{
    ThemeSetData *d = user_data;
    GtkComboBoxText *cb = d->cb;
    GtkEntry *entry = d->entry;
    GtkLabel *status = d->status;
    const char *setting = d->setting_name;

    char *choice = gtk_combo_box_text_get_active_text(cb);
    const char *manual = gtk_editable_get_text(GTK_EDITABLE(entry));
    const char *theme_name = NULL;
    if (choice && *choice) theme_name = choice;
    else if (manual && *manual) theme_name = manual;
    else {
        set_status(status, "Please select or enter a theme name");
        g_free(choice);
        return;
    }

    if (g_strcmp0(setting, "gtk-cursor-theme-name") == 0) {
        gboolean all_ok = TRUE;
        GError *err = NULL;

        gchar *dir = g_build_filename(g_get_home_dir(), ".icons", "default", NULL);
        g_mkdir_with_parents(dir, 0700);
        gchar *path = g_build_filename(dir, "index.theme", NULL);
        gchar *contents = g_strdup_printf("[Icon Theme]\nInherits=%s\n", theme_name);
        gboolean ok = g_file_set_contents(path, contents, -1, &err);
        if (!ok) {
            gchar *msg = g_strdup_printf("Failed to write %s: %s", path, err ? err->message : "unknown");
            set_status(status, msg);
            g_free(msg);
            g_clear_error(&err);
            all_ok = FALSE;
        }
        g_free(dir);
        g_free(path);
        g_free(contents);

        if (all_ok) {
            GError *settings_err = NULL;
            ok = apply_gtk_settings(setting, theme_name, &settings_err);
            if (!ok) {
                gchar *msg = g_strdup_printf("Failed to save %s to GTK settings: %s", setting, settings_err ? settings_err->message : "unknown");
                set_status(status, msg);
                g_free(msg);
                g_clear_error(&settings_err);
                all_ok = FALSE;
            }
        }

        if (all_ok) {
            set_status(status, "Cursor theme saved to both ~/.icons/default/index.theme and GTK settings files");
            GtkWidget *top = gtk_widget_get_ancestor(GTK_WIDGET(status), GTK_TYPE_WINDOW);
            GtkWindow *parent = GTK_WINDOW(top);
            GtkWindow *dialog = create_modal_window(parent, "Restart required");
            GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_window_set_child(dialog, vbox);
            GtkWidget *msg = gtk_label_new("Cursor theme saved. You may need to restart your session (log out and back in) to see the changes.");
            gtk_label_set_wrap(GTK_LABEL(msg), TRUE);
            gtk_box_append(GTK_BOX(vbox), msg);
            GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            GtkWidget *btn_ok = gtk_button_new_with_label("OK");
            g_signal_connect_swapped(btn_ok, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
            gtk_box_append(GTK_BOX(h), btn_ok);
            gtk_box_append(GTK_BOX(vbox), h);
            gtk_window_present(dialog);
        }
    } else {
        GError *err = NULL;
        gboolean ok = apply_gtk_settings(setting, theme_name, &err);
        if (!ok) {
            gchar *msg = g_strdup_printf("Failed to save %s: %s", setting, err ? err->message : "unknown");
            set_status(status, msg);
            g_free(msg);
            g_clear_error(&err);
        } else {
            gchar *msg = g_strdup_printf("%s saved to ~/.config/gtk-3.0/settings.ini and ~/.config/gtk-4.0/settings.ini", setting);
            set_status(status, msg);
            g_free(msg);
        }
    }

    g_free(choice);
}

static void populate_theme_combobox(GtkComboBoxText *cb, const char *base_path)
{
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GDir *d = g_dir_open(base_path, 0, NULL);
    if (d) {
        const char *name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar *full = g_build_filename(base_path, name, NULL);
            if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
                if (!g_hash_table_contains(seen, name)) {
                    gtk_combo_box_text_append_text(cb, name);
                    g_hash_table_add(seen, g_strdup(name));
                }
            }
            g_free(full);
        }
        g_dir_close(d);
    }
    g_hash_table_destroy(seen);
}

static void on_launch_button_clicked(GtkButton *btn, gpointer user_data)
{
    const char *prog = g_object_get_data(G_OBJECT(btn), "prog");
    GtkLabel *status = GTK_LABEL(g_object_get_data(G_OBJECT(btn), "status"));
    if (!prog) return;
    launch_if_found(prog, status);
}

GtkWidget *create_appearance_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Appearance");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *btn_wallpaper = gtk_button_new_with_label("Wallpaper (waypaper)");
    g_signal_connect(btn_wallpaper, "clicked", G_CALLBACK(on_run_waypaper), status_label);
    gtk_box_append(GTK_BOX(vbox), btn_wallpaper);

    /* GTK theme selector */
    GtkWidget *h_gtk = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_gtk = gtk_label_new("GTK theme:");
    gtk_widget_set_size_request(lbl_gtk, 100, -1);
    gtk_widget_set_halign(lbl_gtk, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(h_gtk), lbl_gtk);
    GtkWidget *cb_gtk = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(cb_gtk, TRUE);
    populate_theme_combobox(GTK_COMBO_BOX_TEXT(cb_gtk), "/usr/share/themes");
    gtk_box_append(GTK_BOX(h_gtk), cb_gtk);
    GtkWidget *entry_gtk = gtk_entry_new();
    gtk_widget_set_hexpand(entry_gtk, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_gtk), "Or enter theme name");
    gtk_box_append(GTK_BOX(h_gtk), entry_gtk);
    GtkWidget *btn_gtk = gtk_button_new_with_label("Set GTK theme");
    ThemeSetData *tsd_gtk = g_new0(ThemeSetData, 1);
    tsd_gtk->cb = GTK_COMBO_BOX_TEXT(cb_gtk);
    tsd_gtk->entry = GTK_ENTRY(entry_gtk);
    tsd_gtk->status = status_label;
    tsd_gtk->setting_name = "gtk-theme-name";
    g_signal_connect(btn_gtk, "clicked", G_CALLBACK(on_set_theme_clicked), tsd_gtk);
    gtk_box_append(GTK_BOX(h_gtk), btn_gtk);
    gtk_box_append(GTK_BOX(vbox), h_gtk);

    /* Icon theme selector */
    GtkWidget *h_icon = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_icon = gtk_label_new("Icon theme:");
    gtk_widget_set_size_request(lbl_icon, 100, -1);
    gtk_widget_set_halign(lbl_icon, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(h_icon), lbl_icon);
    GtkWidget *cb_icon = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(cb_icon, TRUE);
    const char *icon_bases[] = { "/usr/share/icons", "/usr/local/share/icons", NULL };
    for (int i = 0; icon_bases[i]; i++) {
        populate_theme_combobox(GTK_COMBO_BOX_TEXT(cb_icon), icon_bases[i]);
    }
    gtk_box_append(GTK_BOX(h_icon), cb_icon);
    GtkWidget *entry_icon = gtk_entry_new();
    gtk_widget_set_hexpand(entry_icon, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_icon), "Or enter theme name");
    gtk_box_append(GTK_BOX(h_icon), entry_icon);
    GtkWidget *btn_icon = gtk_button_new_with_label("Set icon theme");
    ThemeSetData *tsd_icon = g_new0(ThemeSetData, 1);
    tsd_icon->cb = GTK_COMBO_BOX_TEXT(cb_icon);
    tsd_icon->entry = GTK_ENTRY(entry_icon);
    tsd_icon->status = status_label;
    tsd_icon->setting_name = "gtk-icon-theme-name";
    g_signal_connect(btn_icon, "clicked", G_CALLBACK(on_set_theme_clicked), tsd_icon);
    gtk_box_append(GTK_BOX(h_icon), btn_icon);
    gtk_box_append(GTK_BOX(vbox), h_icon);

    /* Cursor theme selector */
    GtkWidget *h_cursor = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_cursor = gtk_label_new("Cursor theme:");
    gtk_widget_set_size_request(lbl_cursor, 100, -1);
    gtk_widget_set_halign(lbl_cursor, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(h_cursor), lbl_cursor);

    GtkWidget *cb_cursor = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(cb_cursor, TRUE);

    const char *cursor_bases[] = { "/usr/share/icons", "/usr/local/share/icons", "~/.icons", "~/.local/share/icons", NULL };
    GHashTable *seen_cursor = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (int i = 0; cursor_bases[i]; i++) {
        const char *base = cursor_bases[i];
        gchar *expanded = NULL;
        if (base[0] == '~') {
            const char *rest = base + 2;
            expanded = g_build_filename(g_get_home_dir(), rest, NULL);
        } else {
            expanded = g_strdup(base);
        }
        GDir *d = g_dir_open(expanded, 0, NULL);
        if (d) {
            const char *name;
            while ((name = g_dir_read_name(d)) != NULL) {
                gchar *full = g_build_filename(expanded, name, NULL);
                if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
                    if (!g_hash_table_contains(seen_cursor, name)) {
                        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cb_cursor), name);
                        g_hash_table_add(seen_cursor, g_strdup(name));
                    }
                }
                g_free(full);
            }
            g_dir_close(d);
        }
        g_free(expanded);
    }
    g_hash_table_destroy(seen_cursor);

    gtk_box_append(GTK_BOX(h_cursor), cb_cursor);

    GtkWidget *entry_cursor = gtk_entry_new();
    gtk_widget_set_hexpand(entry_cursor, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_cursor), "Or enter theme name manually");
    gtk_box_append(GTK_BOX(h_cursor), entry_cursor);

    GtkWidget *btn_cursor = gtk_button_new_with_label("Set cursor theme");
    ThemeSetData *tsd_cursor = g_new0(ThemeSetData, 1);
    tsd_cursor->cb = GTK_COMBO_BOX_TEXT(cb_cursor);
    tsd_cursor->entry = GTK_ENTRY(entry_cursor);
    tsd_cursor->status = status_label;
    tsd_cursor->setting_name = "gtk-cursor-theme-name";
    g_signal_connect(btn_cursor, "clicked", G_CALLBACK(on_set_theme_clicked), tsd_cursor);
    gtk_box_append(GTK_BOX(h_cursor), btn_cursor);

    gtk_box_append(GTK_BOX(vbox), h_cursor);

    /* Qt5 and Qt6 theme configurators */
    GtkWidget *h_qt = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_qt = gtk_label_new("Qt themes:");
    gtk_widget_set_size_request(lbl_qt, 100, -1);
    gtk_widget_set_halign(lbl_qt, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(h_qt), lbl_qt);

    GtkWidget *btn_qt5 = gtk_button_new_with_label("Configure Qt5 (qt5ct)");
    gtk_widget_set_hexpand(btn_qt5, TRUE);
    g_signal_connect(btn_qt5, "clicked", G_CALLBACK(on_launch_button_clicked), NULL);
    g_object_set_data(G_OBJECT(btn_qt5), "prog", (gpointer)"qt5ct");
    g_object_set_data(G_OBJECT(btn_qt5), "status", status_label);
    gtk_box_append(GTK_BOX(h_qt), btn_qt5);

    GtkWidget *btn_qt6 = gtk_button_new_with_label("Configure Qt6 (qt6ct)");
    gtk_widget_set_hexpand(btn_qt6, TRUE);
    g_signal_connect(btn_qt6, "clicked", G_CALLBACK(on_launch_button_clicked), NULL);
    g_object_set_data(G_OBJECT(btn_qt6), "prog", (gpointer)"qt6ct");
    g_object_set_data(G_OBJECT(btn_qt6), "status", status_label);
    gtk_box_append(GTK_BOX(h_qt), btn_qt6);

    gtk_box_append(GTK_BOX(vbox), h_qt);

    return vbox;
}
