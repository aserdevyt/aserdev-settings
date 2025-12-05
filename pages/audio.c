#include "../common.h"
#include "audio.h"
#include <string.h>
#include <gdk/gdk.h>

/*
 * Audio page with PipeWire integration:
 * - Volume slider that uses `pactl` (works with PipeWire's pulse compatibility)
 * - Mute toggle (disables slider when active)
 * - Periodic refresh to reflect external changes
 * - Non-editable text view showing PipeWire info
 * - Button to refresh PipeWire info
 * - Button to restart PipeWire services
 * - Button to open `pavucontrol`
 */

typedef struct {
    GtkScale    *scale;
    GtkLabel    *vol_label;
    GtkCheckButton *mute_btn;
    GtkLabel    *status;
    GtkTextView *info_tv;
    gint64       last_set_time_us;
    gboolean     refresh_in_progress;
} AudioUI;

/* Helper: parse percentage from `pactl get-sink-volume @DEFAULT_SINK@` output */
static int parse_volume_percent(const char *out)
{
    if (!out) return -1;
    /* find first ' %' and then backtrack to number */
    const char *p = strchr(out, '%');
    while (p) {
        /* back up to find digits before percent */
        const char *q = p;
        while (q > out && (*(q-1) == ' ' || *(q-1) == '\t' || *(q-1) == ',')) q--;
        /* now q-1 is last non-space; find start of number */
        const char *start = q;
        while (start > out && (*(start-1) >= '0' && *(start-1) <= '9')) start--;
        if (start < q) {
            char numbuf[16] = {0};
            size_t len = q - start;
            if (len < sizeof(numbuf)) {
                strncpy(numbuf, start, len);
                numbuf[len] = '\0';
                int v = atoi(numbuf);
                return v;
            }
        }
        /* try next percent sign if parsing failed */
        p = strchr(p+1, '%');
    }
    return -1;
}

/* Helper: run PipeWire info commands and collect output */
static gchar *get_pipewire_info(void)
{
    GString *gs = g_string_new(NULL);
    
    /* pipewire --version */
    gchar *out = NULL, *err = NULL;
    if (g_spawn_command_line_sync("pipewire --version", &out, &err, NULL, NULL)) {
        g_string_append(gs, "=== PipeWire Version ===\n");
        g_string_append(gs, out ? out : "N/A");
        g_string_append(gs, "\n\n");
    }
    if (out) g_free(out);
    if (err) g_free(err);
    
    /* pactl info */
    out = err = NULL;
    if (g_spawn_command_line_sync("pactl info", &out, &err, NULL, NULL)) {
        g_string_append(gs, "=== PulseAudio Info ===\n");
        g_string_append(gs, out ? out : "N/A");
        g_string_append(gs, "\n\n");
    }
    if (out) g_free(out);
    if (err) g_free(err);
    
    /* pactl list short sinks */
    out = err = NULL;
    if (g_spawn_command_line_sync("pactl list short sinks", &out, &err, NULL, NULL)) {
        g_string_append(gs, "=== Sinks (Outputs) ===\n");
        g_string_append(gs, out ? out : "N/A");
        g_string_append(gs, "\n\n");
    }
    if (out) g_free(out);
    if (err) g_free(err);
    
    /* pactl list short sources */
    out = err = NULL;
    if (g_spawn_command_line_sync("pactl list short sources", &out, &err, NULL, NULL)) {
        g_string_append(gs, "=== Sources (Inputs) ===\n");
        g_string_append(gs, out ? out : "N/A");
        g_string_append(gs, "\n\n");
    }
    if (out) g_free(out);
    if (err) g_free(err);
    
    /* pactl list sink-inputs */
    out = err = NULL;
    if (g_spawn_command_line_sync("pactl list sink-inputs", &out, &err, NULL, NULL)) {
        g_string_append(gs, "=== Sink Inputs (Apps) ===\n");
        g_string_append(gs, out ? out : "N/A");
        g_string_append(gs, "\n\n");
    }
    if (out) g_free(out);
    if (err) g_free(err);
    
    /* pactl list source-outputs */
    out = err = NULL;
    if (g_spawn_command_line_sync("pactl list source-outputs", &out, &err, NULL, NULL)) {
        g_string_append(gs, "=== Source Outputs ===\n");
        g_string_append(gs, out ? out : "N/A");
    }
    if (out) g_free(out);
    if (err) g_free(err);
    
    gchar *result = g_string_free(gs, FALSE);
    return result;
}

typedef struct {
    GtkTextView *tv;
    AudioUI *ui;
} UpdateInfoData;

static gboolean update_info_ui_cb(gpointer user_data)
{
    UpdateInfoData *d = (UpdateInfoData *)user_data;
    gchar *info = (gchar *)g_object_get_data(G_OBJECT(d->ui->info_tv), "info_text");
    if (info) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(d->tv);
        gtk_text_buffer_set_text(buf, info, -1);
        /* Do NOT free info here; it's owned by the object data */
        g_object_set_data(G_OBJECT(d->ui->info_tv), "info_text", NULL);
    }
    g_free(d);
    return FALSE;
}

static gpointer fetch_pipewire_info_thread(gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    gchar *info = get_pipewire_info();
    
    UpdateInfoData *d = g_new0(UpdateInfoData, 1);
    d->tv = ui->info_tv;
    d->ui = ui;
    g_object_set_data_full(G_OBJECT(ui->info_tv), "info_text", info, g_free);
    g_idle_add(update_info_ui_cb, d);
    
    ui->refresh_in_progress = FALSE;
    return NULL;
}

static gboolean update_volume_ui_cb(gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    /* This function is no longer used for polling; keep returning TRUE if called. */
    return TRUE;
}

/* one-shot UI update (used via g_idle_add) */
static gboolean update_volume_ui_once(gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    /* If user changed volume recently, skip overwriting for 2s */
    gint64 now = g_get_monotonic_time();
    if (ui->last_set_time_us != 0 && (now - ui->last_set_time_us) < (gint64)2000000) return FALSE;

    gchar *out = NULL, *err = NULL;
    gint exit_status = 0;
    GError *error = NULL;
    if (!g_spawn_command_line_sync("pactl get-sink-volume @DEFAULT_SINK@", &out, &err, &exit_status, &error)) {
        if (err) g_free(err);
        if (out) g_free(out);
        if (error) g_clear_error(&error);
        return FALSE;
    }

    int vol = parse_volume_percent(out);
    if (vol >= 0) {
        if (GTK_IS_RANGE(ui->scale)) {
            gtk_range_set_value(GTK_RANGE(ui->scale), vol);
        } else {
            set_status(ui->status, "Volume widget not ready");
        }
        if (GTK_IS_LABEL(ui->vol_label)) {
            char vl[64];
            g_snprintf(vl, sizeof(vl), "%d%%", vol);
            gtk_label_set_text(ui->vol_label, vl);
        }
    }

    if (out) g_free(out);
    if (err) g_free(err);
    if (error) g_clear_error(&error);
    return FALSE;
}

/* Fetch initial volume and set UI (runs in a thread) */
static gpointer fetch_initial_volume_thread(gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    gchar *out = NULL, *err = NULL;
    gint exit_status = 0;
    GError *error = NULL;

    if (!g_spawn_command_line_sync("pactl get-sink-volume @DEFAULT_SINK@", &out, &err, &exit_status, &error)) {
        g_free(out); g_free(err); g_clear_error(&error);
        ui->refresh_in_progress = FALSE;
        return NULL;
    }

    int vol = parse_volume_percent(out);
    if (vol >= 0) {
        /* schedule one-shot UI update on main thread */
        g_idle_add(update_volume_ui_once, ui);
    }

    if (out) g_free(out);
    if (err) g_free(err);
    if (error) g_clear_error(&error);
    ui->refresh_in_progress = FALSE;
    return NULL;
}

static void on_refresh_clicked(GtkButton *btn, gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    if (ui->refresh_in_progress) {
        set_status(ui->status, "Refresh already in progress");
        return;
    }
    ui->refresh_in_progress = TRUE;
    g_thread_new("audio-refresh", fetch_initial_volume_thread, ui);
}

static gboolean on_periodic_refresh(gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    if (ui->refresh_in_progress) return TRUE; /* keep timeout active */
    ui->refresh_in_progress = TRUE;
    g_thread_new("audio-periodic-refresh", fetch_initial_volume_thread, ui);
    return TRUE;
}

static void set_volume_from_slider(GtkRange *range, gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    int val = (int)gtk_range_get_value(range);
    char cmd[256];
    g_snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%%", val);

    if (g_dry_run) {
        set_status(ui->status, "Dry run: %s", cmd);
        return;
    }

    run_command_and_report(cmd, ui->status);
    char vl[64];
    g_snprintf(vl, sizeof(vl), "%d%%", val);
    gtk_label_set_text(ui->vol_label, vl);
    ui->last_set_time_us = g_get_monotonic_time();
}

static void on_mute_toggled(GtkCheckButton *btn, gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    gboolean active = gtk_check_button_get_active(GTK_CHECK_BUTTON(btn));
    char cmd[256];
    if (active) {
        g_snprintf(cmd, sizeof(cmd), "pactl set-sink-mute @DEFAULT_SINK@ 1");
        /* When muting, also set volume to 0% */
        if (!g_dry_run) {
            run_command_and_report("pactl set-sink-volume @DEFAULT_SINK@ 0%", ui->status);
        }
        /* Set slider to 0 and disable it */
        gtk_range_set_value(GTK_RANGE(ui->scale), 0);
        gtk_label_set_text(ui->vol_label, "0%");
    } else {
        g_snprintf(cmd, sizeof(cmd), "pactl set-sink-mute @DEFAULT_SINK@ 0");
        /* Re-enable slider when unmuting */
    }
    if (g_dry_run) {
        set_status(ui->status, "Dry run: %s", cmd);
    } else {
        run_command_and_report(cmd, ui->status);
    }
    /* Disable slider when mute is on, enable when mute is off */
    gtk_widget_set_sensitive(GTK_WIDGET(ui->scale), !active);
}

static void on_refresh_info_clicked(GtkButton *btn, gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    if (ui->refresh_in_progress) {
        set_status(ui->status, "Refresh already in progress");
        return;
    }
    ui->refresh_in_progress = TRUE;
    g_thread_new("audio-refresh-info", fetch_pipewire_info_thread, ui);
}

static void on_restart_pipewire_clicked(GtkButton *btn, gpointer user_data)
{
    AudioUI *ui = (AudioUI *)user_data;
    const char *cmd = "systemctl --user restart pipewire pipewire-pulse pipewire-media-session wireplumber 2>/dev/null && echo 'PipeWire stuff restarted ✅'";
    
    if (g_dry_run) {
        set_status(ui->status, "Dry run: %s", cmd);
        return;
    }
    
    run_command_and_report(cmd, ui->status);
}

/* Keep pavucontrol button behavior (launch if found) */
static void on_open_pavu(GtkButton *btn, gpointer user_data)
{
    GtkLabel *status = GTK_LABEL(user_data);
    launch_if_found("pavucontrol", status);
}

GtkWidget *create_audio_page(GtkLabel *status_label)
{
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    GtkWidget *title = gtk_label_new("Audio");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), title);

    GtkWidget *desc = gtk_label_new("Adjust system volume (PipeWire via pactl). Additional controls below.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_box_append(GTK_BOX(vbox), desc);

    AudioUI *ui = g_new0(AudioUI, 1);
    ui->status = status_label;

    /* Horizontal box: slider + numeric label */
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(h, TRUE);

    GtkAdjustment *adj = gtk_adjustment_new(100, 0, 150, 1, 10, 0);
    GtkWidget *scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    ui->scale = GTK_SCALE(scale);

    GtkWidget *vol_label = gtk_label_new("100%%");
    ui->vol_label = GTK_LABEL(vol_label);

    gtk_box_append(GTK_BOX(h), scale);
    gtk_box_append(GTK_BOX(h), vol_label);

    /* Mute toggle */
    GtkWidget *mute = gtk_check_button_new_with_label("Mute");
    ui->mute_btn = GTK_CHECK_BUTTON(mute);

    gtk_box_append(GTK_BOX(vbox), h);
    gtk_box_append(GTK_BOX(vbox), mute);

    /* PipeWire info section */
    GtkWidget *info_label = gtk_label_new("PipeWire Information");
    gtk_widget_set_halign(info_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), info_label);

    GtkWidget *sc = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(sc, TRUE);
    gtk_widget_set_vexpand(sc, TRUE);
    gtk_widget_set_size_request(sc, -1, 200);

    GtkWidget *info_tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(info_tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(info_tv), GTK_WRAP_WORD_CHAR);
    ui->info_tv = GTK_TEXT_VIEW(info_tv);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), info_tv);
    gtk_box_append(GTK_BOX(vbox), sc);

    /* Buttons: Refresh Info, Restart PipeWire, and Open pavucontrol */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);

    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh Info");
    GtkWidget *btn_restart = gtk_button_new_with_label("Restart PipeWire");
    GtkWidget *btn_pavu = gtk_button_new_with_label("Open Volume Control (pavucontrol)");

    gtk_box_append(GTK_BOX(button_box), btn_refresh);
    gtk_box_append(GTK_BOX(button_box), btn_restart);
    gtk_box_append(GTK_BOX(button_box), btn_pavu);
    gtk_box_append(GTK_BOX(vbox), button_box);

    /* Connect signals */
    g_signal_connect(scale, "value-changed", G_CALLBACK(set_volume_from_slider), ui);
    g_signal_connect(mute, "toggled", G_CALLBACK(on_mute_toggled), ui);
    g_signal_connect(btn_pavu, "clicked", G_CALLBACK(on_open_pavu), status_label);
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_info_clicked), ui);
    g_signal_connect(btn_restart, "clicked", G_CALLBACK(on_restart_pipewire_clicked), ui);

    /* Start periodic refresh every 3000ms (runs in background thread) */
    g_timeout_add_seconds(3, on_periodic_refresh, ui);

    /* Fetch initial volume async (one-shot) */
    ui->refresh_in_progress = TRUE;
    g_thread_new("audio-init", fetch_initial_volume_thread, ui);

    /* Fetch initial PipeWire info async */
    ui->refresh_in_progress = TRUE;
    g_thread_new("audio-init-info", fetch_pipewire_info_thread, ui);

    return vbox;
}
