#include "../common.h"
#include <gtk/gtk.h>
#include <pwd.h>

/* Forward declarations for dialog handlers implemented later in this file */
static void on_add_user_submit(GtkButton *btn, gpointer user_data);
static void on_add_user_cancel(GtkButton *btn, gpointer user_data);
static void open_add_user_dialog(GtkWindow *parent, GtkLabel *status);
static void open_delete_user_dialog(GtkWindow *parent, GtkLabel *status);
static void open_change_pass_dialog(GtkWindow *parent, GtkLabel *status);
static void on_delete_user_submit(GtkButton *btn, gpointer user_data);
static void on_delete_user_cancel(GtkButton *btn, gpointer user_data);
static void on_change_pass_submit(GtkButton *btn, gpointer user_data);
static void on_change_pass_cancel(GtkButton *btn, gpointer user_data);
static void on_user_delete_clicked(GtkButton *btn, gpointer user_data);
static void on_user_password_clicked(GtkButton *btn, gpointer user_data);

/* User action callbacks for list rows */
typedef struct {
    char *username;  /* allocated string, will be freed */
    GtkLabel *status;
} UserActionData;

static void user_action_data_free(gpointer data, GClosure *closure)
{
    UserActionData *uad = (UserActionData *)data;
    if (uad) {
        g_free(uad->username);
        g_free(uad);
    }
}

/* Forward: refresh and manual action helpers */
typedef struct {
    GtkWindow *parent;
    GtkWidget *user_list; /* GtkBox container for user rows */
    GtkLabel  *status;
} UsersPageData;

static void populate_user_list(GtkWidget *user_list, GtkLabel *status_label);

static void on_refresh_users_clicked(GtkButton *btn, gpointer user_data);
static void on_manual_change_pass_clicked(GtkButton *btn, gpointer user_data);
static void on_manual_delete_clicked(GtkButton *btn, gpointer user_data);

static void on_refresh_users_clicked(GtkButton *btn, gpointer user_data)
{
    UsersPageData *upd = user_data;
    if (!upd || !upd->user_list) return;
    populate_user_list(upd->user_list, upd->status);
}

static void on_manual_change_pass_clicked(GtkButton *btn, gpointer user_data)
{
    UsersPageData *upd = user_data;
    if (!upd) return;
    open_change_pass_dialog(upd->parent, upd->status);
}

static void on_manual_delete_clicked(GtkButton *btn, gpointer user_data)
{
    UsersPageData *upd = user_data;
    if (!upd) return;
    open_delete_user_dialog(upd->parent, upd->status);
}

static void populate_user_list(GtkWidget *user_list, GtkLabel *status_label)
{
    /* Remove any existing children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(user_list)) != NULL) {
        gtk_box_remove(GTK_BOX(user_list), child);
    }

    struct passwd *pw;
    setpwent();
    while ((pw = getpwent()) != NULL) {
        /* Skip system users (UID < 1000) */
        if (pw->pw_uid < 1000) continue;

        GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_hexpand(h, TRUE);

        GtkWidget *user_label = gtk_label_new(pw->pw_name);
        gtk_widget_set_halign(user_label, GTK_ALIGN_START);
        gtk_widget_set_hexpand(user_label, TRUE);
        gtk_box_append(GTK_BOX(h), user_label);

        GtkWidget *btn_pass = gtk_button_new_with_label("Change Password");
        UserActionData *pass_data = g_new0(UserActionData, 1);
        pass_data->username = g_strdup(pw->pw_name);
        pass_data->status = status_label;
        g_signal_connect_data(btn_pass, "clicked", G_CALLBACK(on_user_password_clicked), pass_data, user_action_data_free, 0);
        gtk_box_append(GTK_BOX(h), btn_pass);

        GtkWidget *btn_delete = gtk_button_new_with_label("Delete");
        UserActionData *del_data = g_new0(UserActionData, 1);
        del_data->username = g_strdup(pw->pw_name);
        del_data->status = status_label;
        g_signal_connect_data(btn_delete, "clicked", G_CALLBACK(on_user_delete_clicked), del_data, user_action_data_free, 0);
        gtk_box_append(GTK_BOX(h), btn_delete);

        gtk_box_append(GTK_BOX(user_list), h);
    }
    endpwent();
}

static void on_user_delete_clicked(GtkButton *btn, gpointer user_data)
{
    UserActionData *data = user_data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW));
    GtkWindow *dialog = create_modal_window(parent, "Delete User");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(dialog, vbox);

    GtkWidget *msg = gtk_label_new(NULL);
    char msg_text[256];
    snprintf(msg_text, sizeof(msg_text), "Delete user '%s'?", data->username);
    gtk_label_set_text(GTK_LABEL(msg), msg_text);
    gtk_box_append(GTK_BOX(vbox), msg);

    /* Hidden entry containing the username for the submit handler to read */
    GtkWidget *entry_user = gtk_entry_new();
    /* GtkEntry in GTK4 does not declare gtk_entry_set_text; use gtk_editable_set_text */
    gtk_editable_set_text(GTK_EDITABLE(entry_user), data->username);
    gtk_widget_set_visible(entry_user, FALSE);
    gtk_box_append(GTK_BOX(vbox), entry_user);

    GtkWidget *chk_remove_home = gtk_check_button_new_with_label("Remove home directory");
    gtk_box_append(GTK_BOX(vbox), chk_remove_home);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_ok = gtk_button_new_with_label("Delete");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(hbox), btn_ok);
    gtk_box_append(GTK_BOX(hbox), btn_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gpointer *ud = g_new(gpointer, 4);
    ud[0] = dialog;
    ud[1] = entry_user;
    ud[2] = chk_remove_home;
    ud[3] = data->status;

    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_delete_user_submit), ud);
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_delete_user_cancel), dialog);

    gtk_window_present(dialog);
}

static void on_user_password_clicked(GtkButton *btn, gpointer user_data)
{
    UserActionData *data = user_data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW));
    GtkWindow *dialog = create_modal_window(parent, "Change Password");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(dialog, vbox);

    GtkWidget *msg = gtk_label_new(NULL);
    char msg_text[256];
    snprintf(msg_text, sizeof(msg_text), "New password for '%s':", data->username);
    gtk_label_set_text(GTK_LABEL(msg), msg_text);
    gtk_box_append(GTK_BOX(vbox), msg);

    /* Hidden entry containing the username for the submit handler */
    GtkWidget *entry_user = gtk_entry_new();
    /* GtkEntry in GTK4 does not declare gtk_entry_set_text; use gtk_editable_set_text */
    gtk_editable_set_text(GTK_EDITABLE(entry_user), data->username);
    gtk_widget_set_visible(entry_user, FALSE);
    gtk_box_append(GTK_BOX(vbox), entry_user);

    GtkWidget *entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "new password");
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    gtk_box_append(GTK_BOX(vbox), entry_pass);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_ok = gtk_button_new_with_label("Change");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(hbox), btn_ok);
    gtk_box_append(GTK_BOX(hbox), btn_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gpointer *ud = g_new(gpointer, 4);
    ud[0] = dialog;
    ud[1] = entry_user;
    ud[2] = entry_pass;
    ud[3] = data->status;

    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_change_pass_submit), ud);
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_change_pass_cancel), dialog);

    gtk_window_present(dialog);
}

/* Add user UI and handler (moved from main_old.c) */
static void on_add_user_submit(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkWindow *dialog = GTK_WINDOW(ud[0]);
    GtkEntry *entry_user = GTK_ENTRY(ud[1]);
    GtkEntry *entry_pass = GTK_ENTRY(ud[2]);
    GtkCheckButton *chk_sudo = GTK_CHECK_BUTTON(ud[3]);
    GtkCheckButton *chk_passless = GTK_CHECK_BUTTON(ud[4]);
    GtkLabel *status = GTK_LABEL(ud[5]);

    const char *username = gtk_editable_get_text(GTK_EDITABLE(entry_user));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(entry_pass));
    gboolean add_sudo = gtk_check_button_get_active(chk_sudo);
    gboolean passless = gtk_check_button_get_active(chk_passless);

    if (!username || strlen(username) == 0) {
        set_status(status, "Please enter a username");
        return;
    }
    if (user_exists(username)) {
        set_status(status, "User '%s' already exists", username);
        return;
    }

    /* Build script */
    char *sq_user = g_shell_quote(username);
    char *sq_pass = g_shell_quote(password ? password : "");

    const char *sudo_group = find_sudo_group();
    char *script = NULL;
    if (add_sudo && sudo_group) {
        if (passless) {
            script = g_strdup_printf(
                "#!/bin/bash\nset -e\nuseradd -m -s /bin/zsh %s\necho %s:%s | chpasswd\nusermod -aG %s %s\necho %s ALL=(ALL) NOPASSWD: ALL > /etc/sudoers.d/%s\nchmod 0440 /etc/sudoers.d/%s\necho 'OK'\n",
                sq_user, sq_user, sq_pass, sudo_group, sq_user, sq_user, sq_user, sq_user);
        } else {
            script = g_strdup_printf(
                "#!/bin/bash\nset -e\nuseradd -m -s /bin/zsh %s\necho %s:%s | chpasswd\nusermod -aG %s %s\necho 'OK'\n",
                sq_user, sq_user, sq_pass, sudo_group, sq_user);
        }
    } else if (add_sudo && !sudo_group) {
        /* no sudo group found; still create user */
        script = g_strdup_printf(
            "#!/bin/bash\nset -e\nuseradd -m -s /bin/zsh %s\necho %s:%s | chpasswd\necho 'OK'\n",
            sq_user, sq_user, sq_pass);
    } else {
        script = g_strdup_printf(
            "#!/bin/bash\nset -e\nuseradd -m -s /bin/zsh %s\necho %s:%s | chpasswd\necho 'OK'\n",
            sq_user, sq_user, sq_pass);
    }

    gboolean launched = run_privileged_script(script, status);

    g_free(sq_user);
    g_free(sq_pass);
    g_free(script);

    if (launched) {
        set_status(status, "Add user command launched");
        gtk_window_destroy(dialog);
    }
}

static void on_add_user_cancel(GtkButton *btn, gpointer user_data)
{
    GtkWindow *dialog = GTK_WINDOW(user_data);
    gtk_window_destroy(dialog);
}

static void open_add_user_dialog(GtkWindow *parent, GtkLabel *status)
{
    GtkWindow *dialog = create_modal_window(parent, "Add User");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(dialog, vbox);

    GtkWidget *entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_user), "username");
    gtk_box_append(GTK_BOX(vbox), entry_user);

    GtkWidget *entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "password");
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    gtk_box_append(GTK_BOX(vbox), entry_pass);

    GtkWidget *chk_sudo = gtk_check_button_new_with_label("Add to sudo/wheel group");
    gtk_box_append(GTK_BOX(vbox), chk_sudo);
    GtkWidget *chk_passless = gtk_check_button_new_with_label("Enable passwordless sudo");
    gtk_box_append(GTK_BOX(vbox), chk_passless);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_ok = gtk_button_new_with_label("Add");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(hbox), btn_ok);
    gtk_box_append(GTK_BOX(hbox), btn_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gpointer *ud = g_new(gpointer, 6);
    ud[0] = dialog;
    ud[1] = entry_user;
    ud[2] = entry_pass;
    ud[3] = chk_sudo;
    ud[4] = chk_passless;
    ud[5] = status;

    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_add_user_submit), ud);
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_add_user_cancel), dialog);

    gtk_window_present(dialog);
}

/* Delete user */
static void on_delete_user_submit(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkWindow *dialog = GTK_WINDOW(ud[0]);
    GtkEntry *entry_user = GTK_ENTRY(ud[1]);
    GtkCheckButton *chk_remove_home = GTK_CHECK_BUTTON(ud[2]);
    GtkLabel *status = GTK_LABEL(ud[3]);

    const char *username = gtk_editable_get_text(GTK_EDITABLE(entry_user));
    gboolean remove_home = gtk_check_button_get_active(chk_remove_home);

    if (!username || strlen(username) == 0) {
        set_status(status, "Please enter a username");
        return;
    }
    if (!user_exists(username)) {
        set_status(status, "User '%s' does not exist", username);
        return;
    }

    char *sq_user = g_shell_quote(username);
    char *script = NULL;
    if (remove_home) {
        script = g_strdup_printf("#!/bin/bash\nset -e\nuserdel -r %s\nrm -f /etc/sudoers.d/%s\necho 'OK'\n", sq_user, sq_user);
    } else {
        script = g_strdup_printf("#!/bin/bash\nset -e\nuserdel %s\nrm -f /etc/sudoers.d/%s\necho 'OK'\n", sq_user, sq_user);
    }

    gboolean launched = run_privileged_script(script, status);
    g_free(sq_user);
    g_free(script);

    if (launched) {
        set_status(status, "Delete user command launched");
        gtk_window_destroy(dialog);
    }
}

static void on_delete_user_cancel(GtkButton *btn, gpointer user_data)
{
    GtkWindow *dialog = GTK_WINDOW(user_data);
    gtk_window_destroy(dialog);
}

static void open_delete_user_dialog(GtkWindow *parent, GtkLabel *status)
{
    GtkWindow *dialog = create_modal_window(parent, "Delete User");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(dialog, vbox);

    GtkWidget *entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_user), "username");
    gtk_box_append(GTK_BOX(vbox), entry_user);

    GtkWidget *chk_remove_home = gtk_check_button_new_with_label("Remove home directory");
    gtk_box_append(GTK_BOX(vbox), chk_remove_home);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_ok = gtk_button_new_with_label("Delete");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(hbox), btn_ok);
    gtk_box_append(GTK_BOX(hbox), btn_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gpointer *ud = g_new(gpointer, 4);
    ud[0] = dialog;
    ud[1] = entry_user;
    ud[2] = chk_remove_home;
    ud[3] = status;

    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_delete_user_submit), ud);
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_delete_user_cancel), dialog);

    gtk_window_present(dialog);
}

/* Change password dialog */
static void on_change_pass_submit(GtkButton *btn, gpointer user_data)
{
    gpointer *ud = user_data;
    GtkWindow *dialog = GTK_WINDOW(ud[0]);
    GtkEntry *entry_user = GTK_ENTRY(ud[1]);
    GtkEntry *entry_pass = GTK_ENTRY(ud[2]);
    GtkLabel *status = GTK_LABEL(ud[3]);

    const char *username = gtk_editable_get_text(GTK_EDITABLE(entry_user));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(entry_pass));

    if (!username || strlen(username) == 0) {
        set_status(status, "Please enter a username");
        return;
    }
    if (!user_exists(username)) {
        set_status(status, "User '%s' does not exist", username);
        return;
    }

    char *sq_user = g_shell_quote(username);
    char *sq_pass = g_shell_quote(password ? password : "");
    char *script = g_strdup_printf("#!/bin/bash\nset -e\necho %s:%s | chpasswd\necho 'OK'\n", sq_user, sq_pass);

    gboolean launched = run_privileged_script(script, status);
    g_free(sq_user);
    g_free(sq_pass);
    g_free(script);

    if (launched) {
        set_status(status, "Change password command launched");
        gtk_window_destroy(dialog);
    }
}

static void on_change_pass_cancel(GtkButton *btn, gpointer user_data)
{
    GtkWindow *dialog = GTK_WINDOW(user_data);
    gtk_window_destroy(dialog);
}

static void open_change_pass_dialog(GtkWindow *parent, GtkLabel *status)
{
    GtkWindow *dialog = create_modal_window(parent, "Change Password");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(dialog, vbox);

    GtkWidget *entry_user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_user), "username");
    gtk_box_append(GTK_BOX(vbox), entry_user);

    GtkWidget *entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "new password");
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    gtk_box_append(GTK_BOX(vbox), entry_pass);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_ok = gtk_button_new_with_label("Change");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
    gtk_box_append(GTK_BOX(hbox), btn_ok);
    gtk_box_append(GTK_BOX(hbox), btn_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gpointer *ud = g_new(gpointer, 4);
    ud[0] = dialog;
    ud[1] = entry_user;
    ud[2] = entry_pass;
    ud[3] = status;

    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_change_pass_submit), ud);
    g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_change_pass_cancel), dialog);

    gtk_window_present(dialog);
}

GtkWidget *create_users_page(GtkWindow *parent, GtkLabel *status_label)
{
    DBG("create_users_page called");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *label = gtk_label_new("Users manager");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    /* Scrolled window for user list */
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroller);

    GtkWidget *user_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), user_list);

    /* Create page-local data for refresh/manual actions */
    UsersPageData *upd = g_new0(UsersPageData, 1);
    upd->parent = parent;
    upd->user_list = user_list;
    upd->status = GTK_LABEL(status_label);

    /* Top control bar: Refresh + manual Change Password + manual Delete */
    GtkWidget *topbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    GtkWidget *btn_manual_pass = gtk_button_new_with_label("Change Password (manual)");
    GtkWidget *btn_manual_delete = gtk_button_new_with_label("Delete User (manual)");
    gtk_box_append(GTK_BOX(topbar), btn_refresh);
    gtk_box_append(GTK_BOX(topbar), btn_manual_pass);
    gtk_box_append(GTK_BOX(topbar), btn_manual_delete);
    gtk_box_append(GTK_BOX(vbox), topbar);

    /* Connect handlers to the UsersPageData */
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_users_clicked), upd);
    g_signal_connect(btn_manual_pass, "clicked", G_CALLBACK(on_manual_change_pass_clicked), upd);
    g_signal_connect(btn_manual_delete, "clicked", G_CALLBACK(on_manual_delete_clicked), upd);

    /* Initially populate list */
    populate_user_list(user_list, GTK_LABEL(status_label));

    /* New User button at bottom */
    GtkWidget *h_new = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_new = gtk_button_new_with_label("New User");
    gtk_widget_set_hexpand(btn_new, TRUE);
    g_signal_connect(btn_new, "clicked", G_CALLBACK(open_add_user_dialog), status_label);
    gtk_box_append(GTK_BOX(h_new), btn_new);
    gtk_widget_set_halign(h_new, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(vbox), h_new);

    return vbox;
}
