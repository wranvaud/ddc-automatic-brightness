/*
 * power_management.c - Power management and suspend/resume handling implementation
 */

#include "power_management.h"
#include "brightness_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

/* Check if systemd is available for power management */
#ifdef HAVE_SYSTEMD
    #include <systemd/sd-login.h>
    #include <systemd/sd-bus.h>
    #define HAVE_POWER_MANAGEMENT 1
#else
    #define HAVE_POWER_MANAGEMENT 0
#endif

/* Power manager structure */
struct _PowerManager {
    gboolean suspend_resume_supported;
    gboolean system_suspended;
    gboolean screen_blanked;

    /* Saved brightness state for restoration after resume */
    GHashTable *saved_brightness_states;  /* device_path -> GINT_TO_POINTER(brightness) */

    /* systemd/logind integration */
    #if HAVE_POWER_MANAGEMENT
        sd_login_monitor *sd_login_monitor;
        sd_bus *system_bus;
        guint dbus_watch_id;
    #else
        void *sd_login_monitor;
        void *system_bus;
        guint dbus_watch_id;
    #endif

    /* Session bus screensaver signal subscriptions */
    GDBusConnection *session_bus;
    guint screensaver_watch_id;
    guint gnome_screensaver_watch_id;
};

/* Create new power manager */
PowerManager* power_manager_new(void)
{
    PowerManager *manager = g_new0(PowerManager, 1);
    
    manager->suspend_resume_supported = FALSE;
    manager->system_suspended = FALSE;
    manager->saved_brightness_states = g_hash_table_new_full(g_str_hash, g_str_equal, 
                                                             g_free, NULL);
    
    /* Initialize all pointers to NULL */
    manager->sd_login_monitor = NULL;
    manager->system_bus = NULL;
    manager->dbus_watch_id = 0;
    manager->session_bus = NULL;
    manager->screensaver_watch_id = 0;
    manager->gnome_screensaver_watch_id = 0;
    manager->system_dbus = NULL;
    manager->login1_watch_id = 0;
    manager->on_suspend_cb = NULL;
    manager->on_resume_cb = NULL;
    manager->cb_data = NULL;

    return manager;
}

/* Free power manager */
void power_manager_free(PowerManager *manager)
{
    if (manager) {
        power_manager_cleanup_monitoring(manager);
        
        if (manager->saved_brightness_states) {
            g_hash_table_destroy(manager->saved_brightness_states);
        }
        
        g_free(manager);
    }
}

/* Save current brightness state for all monitors */
void power_manager_save_brightness_state(PowerManager *manager, GList *monitors)
{
    if (!manager || !monitors) {
        return;
    }
    
    g_message("Saving brightness state before suspend...");
    
    /* Clear any existing saved state */
    g_hash_table_remove_all(manager->saved_brightness_states);
    
    /* Save brightness for each monitor */
    for (GList *iter = monitors; iter != NULL; iter = iter->next) {
        Monitor *monitor = (Monitor *)iter->data;
        if (monitor && monitor_is_available(monitor)) {
            int brightness = monitor_get_current_brightness(monitor);
            if (brightness >= 0) {
                const char *device_path = monitor_get_device_path(monitor);
                if (device_path) {
                    g_message("Saved brightness %d%% for monitor %s", brightness, device_path);
                    g_hash_table_insert(manager->saved_brightness_states, 
                                       g_strdup(device_path), 
                                       GINT_TO_POINTER(brightness));
                }
            }
        }
    }
    
    g_message("Saved brightness state for %d monitors", 
              g_hash_table_size(manager->saved_brightness_states));
}

/* Restore brightness state to all monitors */
void power_manager_restore_brightness_state(PowerManager *manager, GList *monitors)
{
    if (!manager || !monitors || g_hash_table_size(manager->saved_brightness_states) == 0) {
        return;
    }
    
    g_message("Restoring brightness state after resume...");
    
    int restored_count = 0;
    
    /* Restore brightness for each monitor */
    for (GList *iter = monitors; iter != NULL; iter = iter->next) {
        Monitor *monitor = (Monitor *)iter->data;
        if (monitor && monitor_is_available(monitor)) {
            const char *device_path = monitor_get_device_path(monitor);
            if (device_path) {
                gpointer brightness_ptr = g_hash_table_lookup(manager->saved_brightness_states, 
                                                             device_path);
                if (brightness_ptr) {
                    int brightness = GPOINTER_TO_INT(brightness_ptr);
                    gboolean success = monitor_set_brightness(monitor, brightness);
                    
                    if (success) {
                        g_message("Restored brightness %d%% for monitor %s", brightness, device_path);
                        restored_count++;
                    } else {
                        g_warning("Failed to restore brightness %d%% for monitor %s", 
                                  brightness, device_path);
                    }
                }
            }
        }
    }
    
    g_message("Restored brightness state for %d/%d monitors", 
              restored_count, g_hash_table_size(manager->saved_brightness_states));
}

/* Check if system is suspended */
gboolean power_manager_is_system_suspended(PowerManager *manager)
{
    return manager ? manager->system_suspended : FALSE;
}

/* Check if the screensaver/DPMS blank is currently active */
gboolean power_manager_is_screen_blanked(PowerManager *manager)
{
    return manager ? manager->screen_blanked : FALSE;
}

/* GDBus signal handler for org.freedesktop.ScreenSaver and org.gnome.ScreenSaver
 * ActiveChanged(b active) — fired by virtually all Linux DEs when the screen
 * blanks or unblanks (GNOME Shell, KDE kscreenlocker, XFCE, Cinnamon, MATE, …). */
static void on_screensaver_active_changed(GDBusConnection *connection,
                                          const gchar     *sender_name,
                                          const gchar     *object_path,
                                          const gchar     *interface_name,
                                          const gchar     *signal_name,
                                          GVariant        *parameters,
                                          gpointer         user_data)
{
    (void)connection; (void)sender_name; (void)object_path;
    (void)signal_name;

    PowerManager *manager = (PowerManager *)user_data;
    if (!manager) return;

    gboolean active = FALSE;
    g_variant_get(parameters, "(b)", &active);

    manager->screen_blanked = active;
    g_message("Screen %s (signal: %s)", active ? "blanked" : "unblanked", interface_name);
}

/* Subscribe to screensaver ActiveChanged signals on the session bus.
 * Called from power_manager_setup_monitoring(); safe to call even when
 * the systemd block below fails. */
static void setup_screensaver_monitoring(PowerManager *manager)
{
    GError *error = NULL;
    manager->session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!manager->session_bus) {
        g_warning("Could not connect to session bus for screensaver monitoring: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }

    /* org.freedesktop.ScreenSaver — implemented by GNOME Shell, KDE, XFCE, Cinnamon … */
    manager->screensaver_watch_id = g_dbus_connection_signal_subscribe(
        manager->session_bus,
        NULL,                               /* any sender */
        "org.freedesktop.ScreenSaver",
        "ActiveChanged",
        "/org/freedesktop/ScreenSaver",
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_screensaver_active_changed,
        manager,
        NULL
    );

    /* org.gnome.ScreenSaver — legacy interface, GNOME < 3.8 / gnome-screensaver */
    manager->gnome_screensaver_watch_id = g_dbus_connection_signal_subscribe(
        manager->session_bus,
        NULL,
        "org.gnome.ScreenSaver",
        "ActiveChanged",
        "/org/gnome/ScreenSaver",
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_screensaver_active_changed,
        manager,
        NULL
    );

    g_message("Screensaver/DPMS blank monitoring active (session D-Bus)");
}

/* Register suspend/resume callbacks */
void power_manager_set_callbacks(PowerManager *manager,
                                  void (*on_suspend)(gpointer),
                                  void (*on_resume)(gpointer),
                                  gpointer user_data)
{
    if (!manager) return;
    manager->on_suspend_cb = on_suspend;
    manager->on_resume_cb  = on_resume;
    manager->cb_data       = user_data;
}

/* GDBus handler for org.freedesktop.login1.Manager.PrepareForSleep(b before).
 * before=TRUE  → system is about to suspend.
 * before=FALSE → system has just resumed. */
static void on_login1_prepare_for_sleep(GDBusConnection *connection,
                                         const gchar     *sender_name,
                                         const gchar     *object_path,
                                         const gchar     *interface_name,
                                         const gchar     *signal_name,
                                         GVariant        *parameters,
                                         gpointer         user_data)
{
    (void)connection; (void)sender_name; (void)object_path;
    (void)interface_name; (void)signal_name;

    PowerManager *manager = (PowerManager *)user_data;
    if (!manager) return;

    gboolean before = FALSE;
    g_variant_get(parameters, "(b)", &before);

    if (before) {
        manager->system_suspended = TRUE;
        g_message("System suspend imminent (login1 PrepareForSleep)");
        if (manager->on_suspend_cb)
            manager->on_suspend_cb(manager->cb_data);
    } else {
        manager->system_suspended = FALSE;
        g_message("System resumed (login1 PrepareForSleep done)");
        if (manager->on_resume_cb)
            manager->on_resume_cb(manager->cb_data);
    }
}

/* Subscribe to PrepareForSleep on the system bus. */
static void setup_login1_monitoring(PowerManager *manager)
{
    GError *error = NULL;
    manager->system_dbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!manager->system_dbus) {
        g_warning("Could not connect to system bus for suspend monitoring: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }

    manager->login1_watch_id = g_dbus_connection_signal_subscribe(
        manager->system_dbus,
        "org.freedesktop.login1",
        "org.freedesktop.login1.Manager",
        "PrepareForSleep",
        "/org/freedesktop/login1",
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_login1_prepare_for_sleep,
        manager,
        NULL
    );

    g_message("Suspend/resume monitoring active (system D-Bus login1)");
}

#if HAVE_POWER_MANAGEMENT

/* Callback for systemd suspend/resume events */
static int handle_suspend_resume_event(sd_login_monitor *monitor, 
                                       const char *seat, 
                                       const char *session, 
                                       const char *action, 
                                       void *userdata)
{
    (void)monitor; (void)seat; (void)session;  /* Unused parameters */
    PowerManager *manager = (PowerManager *)userdata;
    
    if (!manager) {
        return 0;
    }
    
    if (strcmp(action, "prepare-for-sleep") == 0) {
        /* System is about to suspend */
        manager->system_suspended = TRUE;
        g_message("System suspend detected");
        
        /* Save current brightness state */
        /* Note: This will be called by the main application when it receives the signal */
        
    } else if (strcmp(action, "resume") == 0) {
        /* System has resumed */
        manager->system_suspended = FALSE;
        g_message("System resume detected");
        
        /* Restore brightness state */
        /* Note: This will be called by the main application when it receives the signal */
        
    }
    
    return 0;
}

/* Setup systemd login monitor */
gboolean power_manager_setup_monitoring(PowerManager *manager)
{
    if (!manager) {
        return FALSE;
    }
    
    /* For now, use a simpler approach since the full systemd integration
     * is complex and may not be available on all systems. 
     * We'll implement basic suspend/resume detection via DBus signals. */
    
    /* Check if we can connect to system bus */
    #if HAVE_POWER_MANAGEMENT
    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    
    if (r >= 0 && bus) {
        manager->suspend_resume_supported = TRUE;
        g_message("Basic suspend/resume monitoring enabled via systemd");
        sd_bus_unref(bus);
    } else {
        g_message("systemd bus connection failed, suspend/resume monitoring disabled");
        manager->suspend_resume_supported = FALSE;
    }

    setup_screensaver_monitoring(manager);
    setup_login1_monitoring(manager);
    return TRUE;
    #else
    g_message("Suspend/resume monitoring not available (systemd not found)");
    manager->suspend_resume_supported = FALSE;
    return FALSE;
    #endif
}

/* Cleanup all monitoring subscriptions */
void power_manager_cleanup_monitoring(PowerManager *manager)
{
    if (!manager) return;

    if (manager->dbus_watch_id > 0) {
        g_source_remove(manager->dbus_watch_id);
        manager->dbus_watch_id = 0;
    }

    if (manager->session_bus) {
        if (manager->screensaver_watch_id > 0)
            g_dbus_connection_signal_unsubscribe(manager->session_bus,
                                                  manager->screensaver_watch_id);
        if (manager->gnome_screensaver_watch_id > 0)
            g_dbus_connection_signal_unsubscribe(manager->session_bus,
                                                  manager->gnome_screensaver_watch_id);
        g_object_unref(manager->session_bus);
        manager->session_bus = NULL;
        manager->screensaver_watch_id = 0;
        manager->gnome_screensaver_watch_id = 0;
    }

    if (manager->system_dbus) {
        if (manager->login1_watch_id > 0)
            g_dbus_connection_signal_unsubscribe(manager->system_dbus,
                                                  manager->login1_watch_id);
        g_object_unref(manager->system_dbus);
        manager->system_dbus = NULL;
        manager->login1_watch_id = 0;
    }

    manager->suspend_resume_supported = FALSE;
}

#else /* !HAVE_POWER_MANAGEMENT */

/* Fallback: libsystemd not compiled in, but GDBus-based monitoring still works */
gboolean power_manager_setup_monitoring(PowerManager *manager)
{
    if (!manager) return FALSE;
    manager->suspend_resume_supported = FALSE;
    setup_screensaver_monitoring(manager);
    setup_login1_monitoring(manager);
    return TRUE;
}

void power_manager_cleanup_monitoring(PowerManager *manager)
{
    if (!manager) return;
    if (manager->session_bus) {
        if (manager->screensaver_watch_id > 0)
            g_dbus_connection_signal_unsubscribe(manager->session_bus,
                                                  manager->screensaver_watch_id);
        if (manager->gnome_screensaver_watch_id > 0)
            g_dbus_connection_signal_unsubscribe(manager->session_bus,
                                                  manager->gnome_screensaver_watch_id);
        g_object_unref(manager->session_bus);
        manager->session_bus = NULL;
        manager->screensaver_watch_id = 0;
        manager->gnome_screensaver_watch_id = 0;
    }
    if (manager->system_dbus) {
        if (manager->login1_watch_id > 0)
            g_dbus_connection_signal_unsubscribe(manager->system_dbus,
                                                  manager->login1_watch_id);
        g_object_unref(manager->system_dbus);
        manager->system_dbus = NULL;
        manager->login1_watch_id = 0;
    }
}

#endif /* HAVE_POWER_MANAGEMENT */