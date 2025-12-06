#include "../common.h"
#include <gtk/gtk.h>

typedef struct {
    GtkWidget  *container; /* GtkBox holding bind rows */
    GtkLabel   *status;    /* status label to report messages */
    char       *path;      /* path to binds.conf */
    GPtrArray  *rows;      /* array of GtkWidget* rows */
    GPtrArray  *original_lines; /* original file lines (preserve comments/blanks) */
} BindsPageData;

static void on_remove_bind_clicked(GtkButton *btn, gpointer user_data);
static void on_add_bind_clicked(GtkButton *btn, gpointer user_data);
static void on_save_binds_clicked(GtkButton *btn, gpointer user_data);

static GtkWidget *create_bind_row(BindsPageData *pd, const char *line)
{
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(h, TRUE);

    GtkWidget *entry = gtk_entry_new();
    if (line) gtk_editable_set_text(GTK_EDITABLE(entry), line);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(h), entry);

    GtkWidget *btn_rm = gtk_button_new_with_label("Remove");
    g_object_set_data(G_OBJECT(btn_rm), "row", h);
    g_signal_connect(btn_rm, "clicked", G_CALLBACK(on_remove_bind_clicked), h);
    gtk_box_append(GTK_BOX(h), btn_rm);

    g_object_set_data(G_OBJECT(h), "binds-pd", pd);

    return h;
}

static char *sanitize_bind_for_display(const char *s)
{
    if (!s) return NULL;
    return g_strdup(s);
}

static void connect_bind_row_remove_button(GtkWidget *row)
{
    GtkWidget *btn = gtk_widget_get_last_child(row);
    if (btn && GTK_IS_BUTTON(btn)) {
        g_signal_handlers_disconnect_by_func(btn, G_CALLBACK(on_remove_bind_clicked), row);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_remove_bind_clicked), row);
    }
}

static void load_binds_file(BindsPageData *pd)
{
    if (pd->rows) {
        for (guint i = 0; i < pd->rows->len; ++i) {
            GtkWidget *r = g_ptr_array_index(pd->rows, i);
            gtk_box_remove(GTK_BOX(pd->container), r);
        }
        g_ptr_array_free(pd->rows, TRUE);
        pd->rows = NULL;
    }

    gchar *content = NULL;
    if (!g_file_get_contents(pd->path, &content, NULL, NULL)) {
        set_status(pd->status, "No binds.conf found at %s — starting empty", pd->path);
        return;
    }

    gchar **lines = g_strsplit(content, "\n", -1);
    pd->original_lines = g_ptr_array_new_with_free_func(g_free);
    pd->rows = g_ptr_array_new();
    for (gint i = 0; lines[i] != NULL; ++i) {
        g_ptr_array_add(pd->original_lines, g_strdup(lines[i]));

        char *trim = g_strdup(lines[i]);
        g_strstrip(trim);
        if (strlen(trim) == 0) { g_free(trim); continue; }
        if (trim[0] == '#') { g_free(trim); continue; }

        char *lower = g_ascii_strdown(trim, -1);
        gboolean is_bind = g_str_has_prefix(lower, "bind");
        g_free(lower);
        g_free(trim);
        if (!is_bind) continue;

        char *disp = sanitize_bind_for_display(lines[i]);
        GtkWidget *row = create_bind_row(pd, disp ? disp : lines[i]);
        g_free(disp);
        gtk_box_append(GTK_BOX(pd->container), row);
        g_ptr_array_add(pd->rows, row);
        connect_bind_row_remove_button(row);
    }
    g_strfreev(lines);
    g_free(content);

    set_status(pd->status, "Loaded %s", pd->path);
}

static void on_remove_bind_clicked(GtkButton *btn, gpointer user_data)
{
    GtkWidget *row = GTK_WIDGET(user_data);
    GtkWidget *parent = gtk_widget_get_parent(row);
    if (parent && GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), row);
        BindsPageData *pd = g_object_get_data(G_OBJECT(row), "binds-pd");
        if (pd && pd->rows) {
            g_ptr_array_remove(pd->rows, row);
        }
    }
}

static void on_add_bind_clicked(GtkButton *btn, gpointer user_data)
{
    BindsPageData *pd = user_data;
    GtkWidget *row = create_bind_row(pd, "");
    connect_bind_row_remove_button(row);
    gtk_box_append(GTK_BOX(pd->container), row);
    if (!pd->rows) pd->rows = g_ptr_array_new();
    g_ptr_array_add(pd->rows, row);
    set_status(pd->status, "Added new bind entry (edit and Save)");
}

static void on_add_comment_clicked(GtkButton *btn, gpointer user_data)
{
    BindsPageData *pd = user_data;
    GtkWidget *row = create_bind_row(pd, "# ");
    connect_bind_row_remove_button(row);
    gtk_box_append(GTK_BOX(pd->container), row);
    if (!pd->rows) pd->rows = g_ptr_array_new();
    g_ptr_array_add(pd->rows, row);
    set_status(pd->status, "Added new comment (edit and Save)");
}

static void on_save_binds_clicked(GtkButton *btn, gpointer user_data)
{
    BindsPageData *pd = user_data;
    GString *out = g_string_new(NULL);
    GtkWidget *first_invalid = NULL;
    GPtrArray *visible_bind_lines = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *visible_comment_lines = g_ptr_array_new_with_free_func(g_free);
    if (pd->rows) {
        for (guint i = 0; i < pd->rows->len; ++i) {
            GtkWidget *row = g_ptr_array_index(pd->rows, i);
            GtkWidget *entry = gtk_widget_get_first_child(row);
            if (entry && GTK_IS_ENTRY(entry)) {
                GtkStyleContext *ctx = gtk_widget_get_style_context(entry);
                gtk_style_context_remove_class(ctx, "invalid-entry");

                const char *txt = gtk_editable_get_text(GTK_EDITABLE(entry));
                const char *use_txt = txt ? txt : "";

                char *trim = g_strdup(use_txt);
                g_strstrip(trim);

                if (strlen(trim) == 0) {
                    g_ptr_array_add(visible_bind_lines, g_strdup(""));
                    g_free(trim);
                    continue;
                }

                if (trim[0] == '#') {
                    g_ptr_array_add(visible_comment_lines, g_strdup(use_txt));
                    g_free(trim);
                    continue;
                }

                char *lower = g_ascii_strdown(trim, -1);
                if (!g_str_has_prefix(lower, "bind")) {
                    gtk_style_context_add_class(ctx, "invalid-entry");
                    if (!first_invalid) first_invalid = entry;
                    g_free(lower);
                    g_free(trim);
                    continue;
                }
                g_free(lower);
                g_free(trim);

                g_ptr_array_add(visible_bind_lines, g_strdup(use_txt));
            }
        }
    }

    if (first_invalid) {
        g_ptr_array_free(visible_bind_lines, TRUE);
        g_ptr_array_free(visible_comment_lines, TRUE);
        set_status(pd->status, "Validation failed: some lines must start with 'bind'");
        gtk_widget_grab_focus(first_invalid);
        return;
    }

    GPtrArray *out_lines = g_ptr_array_new_with_free_func(g_free);
    if (pd->original_lines && pd->original_lines->len > 0) {
        for (guint i = 0; i < pd->original_lines->len; ++i) {
            g_ptr_array_add(out_lines, g_strdup(g_ptr_array_index(pd->original_lines, i)));
        }
    }

    guint bind_index = 0;
    for (guint i = 0; i < out_lines->len; ++i) {
        char *oline = g_strdup(g_ptr_array_index(out_lines, i));
        char *trim = g_strdup(oline);
        g_strstrip(trim);
        if (strlen(trim) == 0 || trim[0] == '#') {
            g_free(oline);
            g_free(trim);
            continue;
        }

        char *lower = g_ascii_strdown(trim, -1);
        gboolean orig_is_bind = g_str_has_prefix(lower, "bind");
        g_free(lower);
        if (!orig_is_bind) {
            g_free(oline);
            g_free(trim);
            continue;
        }

        g_free(g_ptr_array_index(out_lines, i));
        if (bind_index < visible_bind_lines->len) {
            g_ptr_array_index(out_lines, i) = g_strdup(g_ptr_array_index(visible_bind_lines, bind_index));
            bind_index++;
        } else {
            g_ptr_array_index(out_lines, i) = g_strdup("");
        }
        g_free(oline);
        g_free(trim);
    }

    while (bind_index < visible_bind_lines->len) {
        g_ptr_array_add(out_lines, g_strdup(g_ptr_array_index(visible_bind_lines, bind_index)));
        bind_index++;
    }

    for (guint i = 0; i < visible_comment_lines->len; ++i) {
        g_ptr_array_add(out_lines, g_strdup(g_ptr_array_index(visible_comment_lines, i)));
    }

    GString *final = g_string_new(NULL);
    for (guint i = 0; i < out_lines->len; ++i) {
        char *s = g_ptr_array_index(out_lines, i);
        g_string_append(final, s ? s : "");
        if (i + 1 < out_lines->len) g_string_append_c(final, '\n');
    }

    g_ptr_array_free(visible_bind_lines, TRUE);
    g_ptr_array_free(visible_comment_lines, TRUE);
    g_ptr_array_free(out_lines, TRUE);
    g_string_free(out, TRUE);
    out = final;

    if (first_invalid) {
        set_status(pd->status, "Validation failed: some lines must start with 'bind'");
        gtk_widget_grab_focus(first_invalid);
        g_string_free(out, TRUE);
        return;
    }

    GError *error = NULL;
    if (!g_file_set_contents(pd->path, out->str, -1, &error)) {
        set_status(pd->status, "Failed to write %s: %s", pd->path, error->message);
        g_clear_error(&error);
        g_string_free(out, TRUE);
        return;
    }
    g_string_free(out, TRUE);

    set_status(pd->status, "Saved binds to %s", pd->path);
    char *hyprctl = g_find_program_in_path("hyprctl");
    if (hyprctl) {
        char *cmd = g_strdup_printf("%s reload", hyprctl);
        GError *err = NULL;
        gchar *outbuf = NULL;
        gchar *errbuf = NULL;
        gint exit_status = 0;
        gboolean ok = g_spawn_command_line_sync(cmd, &outbuf, &errbuf, &exit_status, &err);
        if (ok && outbuf && *outbuf) {
            GtkWindow *parent = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW));
            GtkWindow *dialog = create_modal_window(parent, "hyprctl output");
            GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_window_set_child(dialog, v);
            GtkWidget *lab = gtk_label_new(outbuf);
            gtk_label_set_selectable(GTK_LABEL(lab), TRUE);
            gtk_box_append(GTK_BOX(v), lab);
            GtkWidget *b = gtk_button_new_with_label("OK");
            g_signal_connect_swapped(b, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
            gtk_box_append(GTK_BOX(v), b);
            gtk_window_present(dialog);
        }
        g_free(cmd);
        g_free(hyprctl);
        g_free(outbuf);
        g_free(errbuf);
        if (err) g_clear_error(&err);
    }
}

GtkWidget *create_binds_page(GtkLabel *status_label)
{
    DBG("create_binds_page called");
    static gboolean css_installed = FALSE;
    if (!css_installed) {
        GtkCssProvider *prov = gtk_css_provider_new();
        const char *css = ".invalid-entry { background-color: #ffdddd; }\n";
        gtk_css_provider_load_from_data(prov, css, -1);
        GdkDisplay *dpy = gdk_display_get_default();
        if (dpy) gtk_style_context_add_provider_for_display(dpy, GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);
        css_installed = TRUE;
    }

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Hyprland Binds Editor");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget *desc = gtk_label_new("Edit your Hyprland binds (one line per bind). Use Add to create a new bind row, Remove to delete a bind, and Save to write to ~/.config/hypr/binds.conf and reload Hyprland.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroller);

    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(container, 6);
    gtk_widget_set_margin_bottom(container, 6);
    gtk_widget_set_hexpand(container, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), container);

    BindsPageData *pd = g_new0(BindsPageData, 1);
    pd->container = container;
    pd->status = GTK_LABEL(status_label);
    pd->path = g_build_filename(g_get_home_dir(), ".config", "hypr", "binds.conf", NULL);

    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_add = gtk_button_new_with_label("Add bind");
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_bind_clicked), pd);
    gtk_box_append(GTK_BOX(h), btn_add);

    GtkWidget *btn_comment = gtk_button_new_with_label("Add comment");
    g_signal_connect(btn_comment, "clicked", G_CALLBACK(on_add_comment_clicked), pd);
    gtk_box_append(GTK_BOX(h), btn_comment);

    GtkWidget *btn_save = gtk_button_new_with_label("Save binds");
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_binds_clicked), pd);
    gtk_box_append(GTK_BOX(h), btn_save);

    gtk_widget_set_halign(h, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), h);

    load_binds_file(pd);

    return vbox;
}
