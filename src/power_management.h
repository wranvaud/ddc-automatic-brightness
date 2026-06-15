/*
 * power_management.h - Power management and suspend/resume handling
 */

#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <glib.h>

G_BEGIN_DECLS

/* Power management state */
typedef struct {
    gboolean suspend_resume_supported;
    gboolean system_suspended;
    gboolean screen_blanked;   /* TRUE while screensaver/DPMS blank is active */

    /* Saved brightness state for restoration after resume */
    GHashTable *saved_brightness_states;  /* device_path -> brightness */

    /* systemd/logind integration */
    void *sd_login_monitor;  /* Opaque pointer to systemd login monitor */
    void *system_bus;        /* Opaque pointer to systemd bus connection */
    guint dbus_watch_id;     /* DBus watch source ID */

    /* Session bus screensaver signal subscriptions */
    void *session_bus;                 /* GDBusConnection* — session bus */
    guint screensaver_watch_id;        /* org.freedesktop.ScreenSaver subscription */
    guint gnome_screensaver_watch_id;  /* org.gnome.ScreenSaver subscription (legacy) */

    /* System bus suspend/resume signal subscription */
    void *system_dbus;        /* GDBusConnection* — system bus */
    guint login1_watch_id;    /* org.freedesktop.login1 PrepareForSleep subscription */

    /* Callbacks fired on suspend / resume */
    void (*on_suspend_cb)(gpointer user_data);
    void (*on_resume_cb)(gpointer user_data);
    gpointer cb_data;

} PowerManager;

/* Initialize power management */
PowerManager* power_manager_new(void);
void power_manager_free(PowerManager *manager);

/* Setup suspend/resume monitoring */
gboolean power_manager_setup_monitoring(PowerManager *manager);
void power_manager_cleanup_monitoring(PowerManager *manager);

/* Save current brightness state for all monitors */
void power_manager_save_brightness_state(PowerManager *manager, GList *monitors);

/* Restore brightness state to all monitors */
void power_manager_restore_brightness_state(PowerManager *manager, GList *monitors);

/* Check if system is suspended */
gboolean power_manager_is_system_suspended(PowerManager *manager);

/* Check if the screensaver/DPMS blank is currently active */
gboolean power_manager_is_screen_blanked(PowerManager *manager);

/* Register suspend/resume callbacks (called from main after setup) */
void power_manager_set_callbacks(PowerManager *manager,
                                  void (*on_suspend)(gpointer),
                                  void (*on_resume)(gpointer),
                                  gpointer user_data);

G_END_DECLS

#endif /* POWER_MANAGEMENT_H */