/*
 * DDC Automatic Brightness - C/GTK Implementation
 * A GUI application for automatic monitor brightness control using DDC/CI
 * 
 * Based on ddccontrol project patterns
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <errno.h>

/* Check if libudev is available - this will be defined by the Makefile */
#ifdef HAVE_LIBUDEV
    #include <libudev.h>
#endif

#ifdef HAVE_LIBAYATANA_APPINDICATOR
#include <libayatana-appindicator/app-indicator.h>
#define HAVE_APPINDICATOR 1
#elif defined(HAVE_LIBAPPINDICATOR)
#include <libappindicator/app-indicator.h>
#define HAVE_APPINDICATOR 1
#else
#define HAVE_APPINDICATOR 0
#endif

#include "brightness_control.h"
#include "monitor_detect.h"
#include "config.h"
#include "scheduler.h"
#include "light_sensor.h"
#include "laptop_backlight.h"
#include "light_sensor_dialog.h"

/* Global application state */
typedef struct {
    GtkWidget *main_window;
    GtkWidget *monitor_combo;
    GtkWidget *brightness_scale;
    GtkWidget *brightness_label;
    GtkWidget *auto_brightness_disabled_radio;
    GtkWidget *auto_brightness_schedule_radio;
    GtkWidget *auto_brightness_sensor_radio;
    GtkWidget *auto_brightness_laptop_radio;
    GtkWidget *schedule_button;
    GtkWidget *curve_button;
    GtkWidget *brightness_offset_scale;
    GtkWidget *brightness_offset_label;
    GtkWidget *start_minimized_check;
    GtkWidget *show_brightness_tray_check;
    GtkWidget *show_light_level_tray_check;

    MonitorList *monitors;
    Monitor *current_monitor;
    AppConfig *config;
    BrightnessScheduler *scheduler;
    LightSensor *light_sensor;
    LaptopBacklight *laptop_backlight;

    gboolean updating_from_auto;
    gboolean in_monitor_refresh;
    guint auto_brightness_timer;
    guint brightness_transition_timer;
    gboolean start_minimized;

    /* Laptop backlight inotify monitoring */
    int laptop_backlight_inotify_fd;
    int laptop_backlight_watch_fd;
    GIOChannel *laptop_backlight_io_channel;
    guint laptop_backlight_watch_id;
    int last_laptop_brightness;

    /* Monitor detection retry state */
    guint monitor_retry_timer;
    int monitor_retry_attempt;
    gboolean monitors_found;
    
    /* Udev monitoring for hardware changes */
#if HAVE_LIBUDEV
    struct udev *udev;
    struct udev_monitor *udev_monitor;
    GIOChannel *udev_io_channel;
    guint udev_watch_id;
#endif
    
#if HAVE_APPINDICATOR
    AppIndicator *indicator;
    GtkWidget *indicator_menu;
#endif
} AppData;

static AppData app_data = {0};

/* Forward declarations */
static void on_window_destroy(GtkWidget *widget, gpointer data);
static void on_monitor_changed(GtkComboBox *combo, gpointer data);
static void on_brightness_changed(GtkRange *range, gpointer data);
static void on_auto_brightness_mode_changed(GtkToggleButton *button, gpointer data);
static void on_brightness_offset_changed(GtkRange *range, gpointer data);
static void on_schedule_clicked(GtkButton *button, gpointer data);
static void on_curve_clicked(GtkButton *button, gpointer data);
static void on_refresh_monitors_clicked(GtkButton *button, gpointer data);
static void load_light_sensor_curve_for_monitor(const char *device_path);
static void on_start_minimized_toggled(GtkToggleButton *button, gpointer data);
static void on_show_brightness_tray_toggled(GtkToggleButton *button, gpointer data);
static void on_show_light_level_tray_toggled(GtkToggleButton *button, gpointer data);
static gboolean auto_brightness_timer_callback(gpointer data);
static gboolean brightness_transition_timer_callback(gpointer data);
static gboolean setup_laptop_backlight_monitoring(void);
static void cleanup_laptop_backlight_monitoring(void);
static gboolean on_laptop_backlight_change(GIOChannel *channel, GIOCondition condition, gpointer data);
static void setup_ui(void);
static void load_monitors(void);
static gboolean load_monitors_with_retry(gpointer data);
static gboolean recheck_monitors_immediately(gpointer data);
static gboolean auto_refresh_monitors_on_failure(void);
static void update_brightness_display(void);
static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
#if HAVE_LIBUDEV
static gboolean setup_udev_monitoring(void);
static void cleanup_udev_monitoring(void);
static gboolean on_udev_event(GIOChannel *channel, GIOCondition condition, gpointer data);
#endif

#if HAVE_APPINDICATOR
static void setup_tray_indicator(void);
static void on_indicator_brightness_20(GtkMenuItem *item, gpointer data);
static void on_indicator_brightness_25(GtkMenuItem *item, gpointer data);
static void on_indicator_brightness_35(GtkMenuItem *item, gpointer data);
static void on_indicator_brightness_50(GtkMenuItem *item, gpointer data);
static void on_indicator_brightness_70(GtkMenuItem *item, gpointer data);
static void on_indicator_brightness_100(GtkMenuItem *item, gpointer data);
static void on_indicator_auto_schedule(GtkMenuItem *item, gpointer data);
static void on_indicator_auto_sensor(GtkMenuItem *item, gpointer data);
static void on_indicator_auto_main_display(GtkMenuItem *item, gpointer data);
static void on_indicator_show_window(GtkMenuItem *item, gpointer data);
static void on_indicator_quit(GtkMenuItem *item, gpointer data);
static void update_indicator_menu(void);
static void on_indicator_menu_show(GtkWidget *menu, gpointer data);
static void update_tray_icon_label(void);
#endif

/* Main entry point */
int main(int argc, char *argv[])
{
    gboolean start_minimized = FALSE;
    gboolean no_gui = FALSE;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tray") == 0 || strcmp(argv[i], "--minimized") == 0) {
            start_minimized = TRUE;
        } else if (strcmp(argv[i], "--no-gui") == 0) {
            no_gui = TRUE;
            start_minimized = TRUE;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("DDC Automatic Brightness (GTK version)\n");
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --tray, --minimized  Start minimized to system tray\n");
            printf("  --no-gui             Run in background (tray only)\n");
            printf("  --help, -h           Show this help\n");
            return 0;
        }
    }
    
    /* Initialize GTK */
    gtk_init(&argc, &argv);
    
    /* Initialize application components */
    app_data.config = config_new();
    if (!config_load(app_data.config)) {
        g_warning("Failed to load configuration, using defaults");
    }
    
    app_data.scheduler = scheduler_new();
    if (!scheduler_load_from_config(app_data.scheduler, app_data.config)) {
        /* Load default schedule */
        scheduler_add_time(app_data.scheduler, 9, 0, 70);   /* 9:00 AM - 70% */
        scheduler_add_time(app_data.scheduler, 11, 0, 80);  /* 11:00 AM - 80% */
        scheduler_add_time(app_data.scheduler, 13, 0, 90);  /* 1:00 PM - 90% */
        scheduler_add_time(app_data.scheduler, 15, 0, 85);  /* 3:00 PM - 85% */
        scheduler_add_time(app_data.scheduler, 17, 0, 70);  /* 5:00 PM - 70% */
        scheduler_add_time(app_data.scheduler, 19, 0, 50);  /* 7:00 PM - 50% */
    }

    /* Initialize light sensor */
    app_data.light_sensor = light_sensor_new();
    if (light_sensor_is_available(app_data.light_sensor)) {
        g_message("Ambient light sensor available for automatic brightness control");
    }

    /* Initialize laptop backlight */
    app_data.laptop_backlight = laptop_backlight_new();
    if (laptop_backlight_is_available(app_data.laptop_backlight)) {
        g_message("Laptop backlight available for automatic brightness control");
    }
    
    /* Check configuration for start minimized */
    if (!start_minimized) {
        start_minimized = config_get_start_minimized(app_data.config);
    }
    app_data.start_minimized = start_minimized;
    
    /* Setup UI */
    setup_ui();
    
    /* Setup tray indicator */
#if HAVE_APPINDICATOR
    setup_tray_indicator();
#endif
    
    /* Setup udev monitoring for hardware changes */
#if HAVE_LIBUDEV
    setup_udev_monitoring();
#endif

    /* Setup laptop backlight monitoring for real-time brightness changes */
    setup_laptop_backlight_monitoring();

    /* Load monitors */
    load_monitors();
    
    /* Update tray icon after initial monitor detection */
#if HAVE_APPINDICATOR
    update_tray_icon_label();
#endif
    
    /* Start timer for menu updates and auto brightness (runs always) */
    app_data.auto_brightness_timer = g_timeout_add_seconds(60,
                                                          auto_brightness_timer_callback,
                                                          &app_data);

    /* Start timer for gradual brightness transitions (runs every 0.5 seconds for smooth 1% steps) */
    app_data.brightness_transition_timer = g_timeout_add(500,  /* 500 milliseconds = 0.5 seconds */
                                                         brightness_transition_timer_callback,
                                                         &app_data);

    /* Show main window unless starting minimized */
    if (!start_minimized || !HAVE_APPINDICATOR) {
        gtk_widget_show_all(app_data.main_window);
    } else {
        /* Hide window if starting minimized and tray is available */
        /* Window will be shown when needed */
    }
    
    /* Start GTK main loop */
    gtk_main();
    
    /* Cleanup */
    if (app_data.auto_brightness_timer > 0) {
        g_source_remove(app_data.auto_brightness_timer);
    }

    if (app_data.brightness_transition_timer > 0) {
        g_source_remove(app_data.brightness_transition_timer);
    }

    if (app_data.monitor_retry_timer > 0) {
        g_source_remove(app_data.monitor_retry_timer);
    }
    
    /* Cleanup udev monitoring */
#if HAVE_LIBUDEV
    cleanup_udev_monitoring();
#endif

    /* Cleanup laptop backlight monitoring */
    cleanup_laptop_backlight_monitoring();

    if (app_data.monitors) {
        monitor_list_free(app_data.monitors);
    }

    if (app_data.scheduler) {
        scheduler_free(app_data.scheduler);
    }

    if (app_data.light_sensor) {
        light_sensor_free(app_data.light_sensor);
    }

    if (app_data.laptop_backlight) {
        laptop_backlight_free(app_data.laptop_backlight);
    }

    if (app_data.config) {
        config_save(app_data.config);
        config_free(app_data.config);
    }

    return 0;
}

/* Window destroy callback */
static void on_window_destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

/* Monitor selection changed */
static void on_monitor_changed(GtkComboBox *combo, gpointer data)
{
    (void)data;

    /* Skip if we're in the middle of a monitor refresh to avoid recursion */
    if (app_data.in_monitor_refresh) {
        g_message("Skipping on_monitor_changed during refresh");
        return;
    }

    gint active = gtk_combo_box_get_active(combo);
    if (active >= 0 && app_data.monitors) {
        app_data.current_monitor = monitor_list_get_monitor(app_data.monitors, active);

        if (app_data.current_monitor) {
            /* Save as default monitor */
            config_set_default_monitor(app_data.config,
                                     monitor_get_device_path(app_data.current_monitor));

            /* Read current brightness */
            int brightness = monitor_get_brightness_with_retry(app_data.current_monitor, auto_refresh_monitors_on_failure);

            /* If refresh was triggered, app_data.current_monitor has been updated to the new monitor.
             * Retry reading brightness from the refreshed monitor. */
            if (brightness < 0 && app_data.current_monitor) {
                g_message("Retrying brightness read after auto-refresh...");
                brightness = monitor_get_brightness(app_data.current_monitor);
                if (brightness >= 0) {
                    g_message("Brightness read successful on retry: %d%%", brightness);
                } else {
                    g_message("Brightness read still failed after retry");
                }
            }

            if (brightness >= 0) {
                app_data.updating_from_auto = TRUE;
                gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), brightness);
                app_data.updating_from_auto = FALSE;
                update_brightness_display();
            }
            
            /* Load auto brightness mode for this monitor */
            AutoBrightnessMode mode = config_get_monitor_auto_brightness_mode(app_data.config,
                                                                              monitor_get_device_path(app_data.current_monitor));

            /* Update radio buttons based on mode */
            switch (mode) {
                case AUTO_BRIGHTNESS_MODE_DISABLED:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio), TRUE);
                    break;
                case AUTO_BRIGHTNESS_MODE_TIME_SCHEDULE:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_schedule_radio), TRUE);
                    break;
                case AUTO_BRIGHTNESS_MODE_LIGHT_SENSOR:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_sensor_radio), TRUE);
                    break;
                case AUTO_BRIGHTNESS_MODE_LAPTOP_DISPLAY:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_laptop_radio), TRUE);
                    break;
            }

            /* Load brightness offset for this monitor */
            int offset = config_get_monitor_brightness_offset(app_data.config,
                                                             monitor_get_device_path(app_data.current_monitor));
            app_data.updating_from_auto = TRUE;
            gtk_range_set_value(GTK_RANGE(app_data.brightness_offset_scale), offset);
            app_data.updating_from_auto = FALSE;

            /* Update offset label */
            char offset_text[16];
            if (offset >= 0) {
                snprintf(offset_text, sizeof(offset_text), "+%d%%", offset);
            } else {
                snprintf(offset_text, sizeof(offset_text), "%d%%", offset);
            }
            gtk_label_set_text(GTK_LABEL(app_data.brightness_offset_label), offset_text);
        }
    }
}

/* Brightness slider changed */
static void on_brightness_changed(GtkRange *range, gpointer data)
{
    if (app_data.updating_from_auto) {
        return;
    }

    if (app_data.current_monitor) {
        int brightness = (int)gtk_range_get_value(range);
        monitor_set_brightness_with_retry(app_data.current_monitor, brightness, auto_refresh_monitors_on_failure);
        update_brightness_display();

        /* Disable auto brightness when user manually adjusts */
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio), TRUE);
        }
    }
}

/* Brightness offset slider changed */
static void on_brightness_offset_changed(GtkRange *range, gpointer data)
{
    (void)data;

    if (app_data.updating_from_auto) {
        return;
    }

    if (!app_data.current_monitor) {
        return;
    }

    int offset = (int)gtk_range_get_value(range);

    /* Update label */
    char text[16];
    if (offset >= 0) {
        snprintf(text, sizeof(text), "+%d%%", offset);
    } else {
        snprintf(text, sizeof(text), "%d%%", offset);
    }
    gtk_label_set_text(GTK_LABEL(app_data.brightness_offset_label), text);

    /* Save offset for this monitor */
    config_set_monitor_brightness_offset(app_data.config,
                                         monitor_get_device_path(app_data.current_monitor),
                                         offset);
    config_save(app_data.config);
}

/* Load the light sensor curve for a specific monitor from config */
static void load_light_sensor_curve_for_monitor(const char *device_path)
{
    if (!device_path || !app_data.light_sensor) {
        return;
    }

    /* Load curve from config */
    LightSensorCurvePoint *points = NULL;
    int count = 0;

    if (config_load_light_sensor_curve(app_data.config, device_path, &points, &count)) {
        /* Successfully loaded curve from config */
        g_message("Loaded %d curve points for monitor %s", count, device_path);

        /* Apply the curve to the light sensor */
        if (count >= 2) {
            /* Use the new flexible API that accepts any number of points */
            light_sensor_set_curve(app_data.light_sensor, points, count);
        }

        g_free(points);
    } else {
        /* No curve configured, use default */
        g_message("No curve configured for monitor %s, using defaults", device_path);
        /* The light sensor already has default values, so nothing to do */
    }
}

/* Auto brightness mode radio button changed */
static void on_auto_brightness_mode_changed(GtkToggleButton *button, gpointer data)
{
    if (!gtk_toggle_button_get_active(button)) {
        /* Ignore deactivation signals, only handle activation */
        return;
    }

    if (!app_data.current_monitor) {
        return;
    }

    /* Determine which mode was selected */
    AutoBrightnessMode mode = AUTO_BRIGHTNESS_MODE_DISABLED;

    if (button == GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio)) {
        mode = AUTO_BRIGHTNESS_MODE_DISABLED;
    } else if (button == GTK_TOGGLE_BUTTON(app_data.auto_brightness_schedule_radio)) {
        mode = AUTO_BRIGHTNESS_MODE_TIME_SCHEDULE;
    } else if (button == GTK_TOGGLE_BUTTON(app_data.auto_brightness_sensor_radio)) {
        mode = AUTO_BRIGHTNESS_MODE_LIGHT_SENSOR;
    } else if (button == GTK_TOGGLE_BUTTON(app_data.auto_brightness_laptop_radio)) {
        mode = AUTO_BRIGHTNESS_MODE_LAPTOP_DISPLAY;
    }

    /* Save setting per monitor */
    config_set_monitor_auto_brightness_mode(app_data.config,
                                            monitor_get_device_path(app_data.current_monitor),
                                            mode);

    /* Apply brightness via gradual transition based on the new mode */
    if (mode == AUTO_BRIGHTNESS_MODE_TIME_SCHEDULE) {
        /* Apply current scheduled brightness via gradual transition */
        int target_brightness = scheduler_get_current_brightness(app_data.scheduler);
        if (target_brightness >= 0) {
            monitor_set_target_brightness(app_data.current_monitor, target_brightness);
            g_message("Scheduled brightness: setting target to %d%%", target_brightness);
        }
    } else if (mode == AUTO_BRIGHTNESS_MODE_LIGHT_SENSOR) {
        /* Apply light sensor-based brightness via gradual transition */
        if (light_sensor_is_available(app_data.light_sensor)) {
            /* Load the curve for this monitor */
            load_light_sensor_curve_for_monitor(monitor_get_device_path(app_data.current_monitor));

            double lux = light_sensor_read_lux(app_data.light_sensor);
            if (lux >= 0) {
                int target_brightness = light_sensor_calculate_brightness(app_data.light_sensor, lux);
                if (target_brightness >= 0) {
                    /* Set stable lux when mode is first enabled */
                    monitor_set_stable_lux(app_data.current_monitor, lux);
                    monitor_set_target_brightness(app_data.current_monitor, target_brightness);
                    g_message("Light sensor: %.1f lux -> %d%% brightness (mode enabled, gradual transition)",
                             lux, target_brightness);
                }
            }
        }
    } else if (mode == AUTO_BRIGHTNESS_MODE_LAPTOP_DISPLAY) {
        /* Apply laptop display-based brightness via gradual transition */
        if (laptop_backlight_is_available(app_data.laptop_backlight)) {
            int laptop_brightness = laptop_backlight_read_brightness(app_data.laptop_backlight);
            if (laptop_brightness >= 0) {
                /* Apply brightness offset */
                int offset = config_get_monitor_brightness_offset(app_data.config,
                                                                  monitor_get_device_path(app_data.current_monitor));
                int target_brightness = laptop_brightness + offset;

                /* Clamp to 0-100 range */
                if (target_brightness < 0) target_brightness = 0;
                if (target_brightness > 100) target_brightness = 100;

                monitor_set_target_brightness(app_data.current_monitor, target_brightness);
                g_message("Laptop display: %d%% + offset %d%% -> %d%% brightness (gradual transition)",
                         laptop_brightness, offset, target_brightness);
            }
        }
    }
}

/* Schedule configuration button clicked */
static void on_schedule_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    /* This will open a schedule configuration dialog */
    /* Implementation in separate file for clarity */
    show_schedule_dialog(app_data.main_window, app_data.scheduler, app_data.config);
}

/* Curve configuration button clicked */
static void on_curve_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;

    if (!app_data.current_monitor) {
        return;
    }

    const char *device_path = monitor_get_device_path(app_data.current_monitor);
    const char *display_name = monitor_get_display_name(app_data.current_monitor);

    /* Open curve configuration dialog */
    show_light_sensor_dialog(app_data.main_window, app_data.config, device_path, display_name);
}

/* Refresh monitors button clicked */
static void on_refresh_monitors_clicked(GtkButton *button, gpointer data)
{
    /* Cancel any pending retry timer */
    if (app_data.monitor_retry_timer > 0) {
        g_source_remove(app_data.monitor_retry_timer);
        app_data.monitor_retry_timer = 0;
    }
    
    /* Set retry attempt to a manual value to show error dialog if no monitors found */
    app_data.monitor_retry_attempt = -2;  /* Special value for manual refresh */
    load_monitors();
    app_data.monitor_retry_attempt = 0;   /* Reset after manual refresh */
}

/* Brightness transition timer callback - handles gradual brightness changes */
static gboolean brightness_transition_timer_callback(gpointer data)
{
    (void)data;  /* Unused parameter */

    /* Process each monitor's transition */
    if (app_data.monitors) {
        for (int i = 0; i < monitor_list_get_count(app_data.monitors); i++) {
            Monitor *monitor = monitor_list_get_monitor(app_data.monitors, i);
            int current = monitor_get_current_brightness(monitor);
            int target = monitor_get_target_brightness(monitor);

            /* Skip if no transition needed */
            if (target < 0 || current == target) {
                continue;
            }

            /* Move one step toward target */
            int next_brightness;
            if (current < 0) {
                /* Unknown current brightness, jump directly to target */
                next_brightness = target;
            } else if (current < target) {
                /* Increase by 1% */
                next_brightness = current + 1;
            } else {
                /* Decrease by 1% */
                next_brightness = current - 1;
            }

            /* Set the brightness */
            monitor_set_brightness_with_retry(monitor, next_brightness, auto_refresh_monitors_on_failure);

            /* Update UI if this is the current monitor */
            if (monitor == app_data.current_monitor) {
                app_data.updating_from_auto = TRUE;
                gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), next_brightness);
                app_data.updating_from_auto = FALSE;
                /* Update the label below the slider to reflect current brightness */
                update_brightness_display();
            }

            /* If we've reached the target, clear it */
            if (next_brightness == target) {
                monitor_set_target_brightness(monitor, -1);
            }
        }
    }

    /* Note: Tray icon and menu are already updated by update_brightness_display()
     * when processing the current monitor above */

    /* Continue timer */
    return TRUE;
}

/* Auto brightness timer callback */
static gboolean auto_brightness_timer_callback(gpointer data)
{
    if (!app_data.current_monitor) {
        return TRUE; /* Continue timer */
    }

    /* Process each monitor based on its auto brightness mode */
    if (app_data.monitors) {
        for (int i = 0; i < monitor_list_get_count(app_data.monitors); i++) {
            Monitor *monitor = monitor_list_get_monitor(app_data.monitors, i);
            AutoBrightnessMode mode = config_get_monitor_auto_brightness_mode(app_data.config,
                                                                              monitor_get_device_path(monitor));

            int target_brightness = -1;

            if (mode == AUTO_BRIGHTNESS_MODE_TIME_SCHEDULE) {
                /* Apply scheduled brightness to this monitor */
                target_brightness = scheduler_get_current_brightness(app_data.scheduler);
            } else if (mode == AUTO_BRIGHTNESS_MODE_LIGHT_SENSOR) {
                /* Apply light sensor-based brightness with hysteresis */
                if (light_sensor_is_available(app_data.light_sensor)) {
                    /* Load the curve for this monitor */
                    load_light_sensor_curve_for_monitor(monitor_get_device_path(monitor));

                    double lux = light_sensor_read_lux(app_data.light_sensor);
                    if (lux >= 0) {
                        /* Get the last stable lux value used for this monitor */
                        double stable_lux = monitor_get_stable_lux(monitor);

                        /* Hysteresis: only change brightness if lux changes by more than ±5 lux */
                        const double LUX_HYSTERESIS = 5.0;
                        gboolean should_update = FALSE;

                        if (stable_lux < 0) {
                            /* First time setting brightness for this monitor */
                            should_update = TRUE;
                        } else if (lux < stable_lux - LUX_HYSTERESIS || lux > stable_lux + LUX_HYSTERESIS) {
                            /* Lux changed significantly, update brightness */
                            should_update = TRUE;
                        }

                        if (should_update) {
                            target_brightness = light_sensor_calculate_brightness(app_data.light_sensor, lux);
                            monitor_set_stable_lux(monitor, lux);

                            if (monitor == app_data.current_monitor) {
                                g_message("Light sensor: %.1f lux -> %d%% brightness (was %.1f lux)",
                                         lux, target_brightness, stable_lux);
                            }
                        } else {
                            /* Within hysteresis zone, keep current brightness target */
                            if (monitor == app_data.current_monitor) {
                                g_debug("Light sensor: %.1f lux within hysteresis zone of %.1f lux (±%.1f), no change",
                                       lux, stable_lux, LUX_HYSTERESIS);
                            }
                            target_brightness = -1;  /* Don't update */
                        }
                    }
                }
            } else if (mode == AUTO_BRIGHTNESS_MODE_LAPTOP_DISPLAY) {
                /* Apply laptop display-based brightness */
                if (laptop_backlight_is_available(app_data.laptop_backlight)) {
                    int laptop_brightness = laptop_backlight_read_brightness(app_data.laptop_backlight);
                    if (laptop_brightness >= 0) {
                        target_brightness = laptop_brightness;

                        /* Apply brightness offset */
                        int offset = config_get_monitor_brightness_offset(app_data.config,
                                                                          monitor_get_device_path(monitor));
                        target_brightness += offset;

                        /* Clamp to 0-100 range */
                        if (target_brightness < 0) target_brightness = 0;
                        if (target_brightness > 100) target_brightness = 100;

                        if (monitor == app_data.current_monitor) {
                            g_message("Laptop display: %d%% + offset %d%% -> %d%% brightness",
                                     laptop_brightness, offset, target_brightness);
                        }
                    }
                }
            }

            /* Set target brightness for gradual transition */
            if (target_brightness >= 0) {
                monitor_set_target_brightness(monitor, target_brightness);

                if (monitor == app_data.current_monitor) {
                    g_debug("Set target brightness to %d%% (current: %d%%) for gradual transition",
                           target_brightness, monitor_get_current_brightness(monitor));
                }
            }
        }
    }

    /* Update tray icon to reflect current brightness and lux values after processing all monitors */
    update_tray_icon_label();

    /* Timer continues running for auto brightness adjustments */
    return TRUE; /* Continue timer */
}

/* Setup the user interface */
static void setup_ui(void)
{
    GtkWidget *vbox, *hbox;
    GtkWidget *monitor_frame, *brightness_frame, *auto_frame, *button_frame;
    GtkWidget *monitor_label, *refresh_button, *quit_button;
    
    /* Create main window */
    app_data.main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app_data.main_window), "DDC Automatic Brightness");
    gtk_window_set_default_size(GTK_WINDOW(app_data.main_window), 500, 400);
    gtk_window_set_position(GTK_WINDOW(app_data.main_window), GTK_WIN_POS_CENTER);
    
    g_signal_connect(app_data.main_window, "destroy", 
                     G_CALLBACK(on_window_destroy), NULL);
    g_signal_connect(app_data.main_window, "delete-event",
                     G_CALLBACK(on_window_delete_event), NULL);
    
    /* Main container */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(app_data.main_window), vbox);
    
    /* Store reference to main vbox for sibling frames */
    GtkWidget *main_vbox = vbox;
    
    /* Monitor selection frame */
    monitor_frame = gtk_frame_new("Monitor");
    gtk_box_pack_start(GTK_BOX(main_vbox), monitor_frame, FALSE, FALSE, 0);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);
    gtk_container_add(GTK_CONTAINER(monitor_frame), hbox);
    
    monitor_label = gtk_label_new("Monitor:");
    gtk_box_pack_start(GTK_BOX(hbox), monitor_label, FALSE, FALSE, 0);
    
    app_data.monitor_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(hbox), app_data.monitor_combo, TRUE, TRUE, 0);
    g_signal_connect(app_data.monitor_combo, "changed", 
                     G_CALLBACK(on_monitor_changed), NULL);
    
    /* Brightness control frame */
    brightness_frame = gtk_frame_new("Brightness Control");
    gtk_box_pack_start(GTK_BOX(main_vbox), brightness_frame, FALSE, FALSE, 0);
    
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(brightness_frame), vbox);
    
    app_data.brightness_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app_data.brightness_scale), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), app_data.brightness_scale, FALSE, FALSE, 0);
    g_signal_connect(app_data.brightness_scale, "value-changed", 
                     G_CALLBACK(on_brightness_changed), NULL);
    
    app_data.brightness_label = gtk_label_new("50%");
    gtk_box_pack_start(GTK_BOX(vbox), app_data.brightness_label, FALSE, FALSE, 0);
    
    /* Auto brightness frame */
    auto_frame = gtk_frame_new("Automatic Brightness");
    gtk_box_pack_start(GTK_BOX(main_vbox), auto_frame, FALSE, FALSE, 0);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(auto_frame), vbox);

    /* Radio buttons for auto brightness mode */
    app_data.auto_brightness_disabled_radio = gtk_radio_button_new_with_label(NULL, "Disabled");
    gtk_box_pack_start(GTK_BOX(vbox), app_data.auto_brightness_disabled_radio, FALSE, FALSE, 0);
    g_signal_connect(app_data.auto_brightness_disabled_radio, "toggled",
                     G_CALLBACK(on_auto_brightness_mode_changed), NULL);

    /* Time-based schedule option with configure button */
    GSList *radio_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app_data.auto_brightness_disabled_radio));
    GtkWidget *schedule_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(vbox), schedule_hbox, FALSE, FALSE, 0);

    app_data.auto_brightness_schedule_radio = gtk_radio_button_new_with_label(radio_group, "Time-based schedule");
    gtk_box_pack_start(GTK_BOX(schedule_hbox), app_data.auto_brightness_schedule_radio, TRUE, TRUE, 0);
    g_signal_connect(app_data.auto_brightness_schedule_radio, "toggled",
                     G_CALLBACK(on_auto_brightness_mode_changed), NULL);

    app_data.schedule_button = gtk_button_new_with_label("Configure Schedule");
    gtk_widget_set_size_request(app_data.schedule_button, 160, -1);
    gtk_box_pack_start(GTK_BOX(schedule_hbox), app_data.schedule_button, FALSE, FALSE, 0);
    g_signal_connect(app_data.schedule_button, "clicked",
                     G_CALLBACK(on_schedule_clicked), NULL);

    /* Ambient light sensor option with configure button */
    radio_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app_data.auto_brightness_schedule_radio));
    GtkWidget *sensor_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(vbox), sensor_hbox, FALSE, FALSE, 0);

    app_data.auto_brightness_sensor_radio = gtk_radio_button_new_with_label(radio_group, "Ambient light sensor");
    gtk_box_pack_start(GTK_BOX(sensor_hbox), app_data.auto_brightness_sensor_radio, TRUE, TRUE, 0);
    g_signal_connect(app_data.auto_brightness_sensor_radio, "toggled",
                     G_CALLBACK(on_auto_brightness_mode_changed), NULL);

    app_data.curve_button = gtk_button_new_with_label("Configure Curve");
    gtk_widget_set_size_request(app_data.curve_button, 160, -1);
    gtk_box_pack_start(GTK_BOX(sensor_hbox), app_data.curve_button, FALSE, FALSE, 0);
    g_signal_connect(app_data.curve_button, "clicked",
                     G_CALLBACK(on_curve_clicked), NULL);

    /* Disable sensor option if no sensor available */
    if (!light_sensor_is_available(app_data.light_sensor)) {
        gtk_widget_set_sensitive(app_data.auto_brightness_sensor_radio, FALSE);
        gtk_widget_set_sensitive(app_data.curve_button, FALSE);
        gtk_widget_set_tooltip_text(app_data.auto_brightness_sensor_radio,
                                   "No ambient light sensor detected on this system");
    }

    /* Follow main display option with brightness offset */
    radio_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app_data.auto_brightness_sensor_radio));
    GtkWidget *laptop_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), laptop_vbox, FALSE, FALSE, 0);

    app_data.auto_brightness_laptop_radio = gtk_radio_button_new_with_label(radio_group, "Follow main display");
    gtk_box_pack_start(GTK_BOX(laptop_vbox), app_data.auto_brightness_laptop_radio, FALSE, FALSE, 0);
    g_signal_connect(app_data.auto_brightness_laptop_radio, "toggled",
                     G_CALLBACK(on_auto_brightness_mode_changed), NULL);

    /* Brightness offset section (indented under "Follow main display") */
    GtkWidget *offset_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(laptop_vbox), offset_hbox, FALSE, FALSE, 0);
    gtk_widget_set_margin_start(offset_hbox, 30);  /* Indent to show it belongs to "Follow main display" */

    GtkWidget *offset_label_text = gtk_label_new("Brightness offset:");
    gtk_box_pack_start(GTK_BOX(offset_hbox), offset_label_text, FALSE, FALSE, 0);

    app_data.brightness_offset_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -20, 20, 1);
    gtk_scale_set_draw_value(GTK_SCALE(app_data.brightness_offset_scale), FALSE);
    gtk_widget_set_size_request(app_data.brightness_offset_scale, 150, -1);
    gtk_box_pack_start(GTK_BOX(offset_hbox), app_data.brightness_offset_scale, TRUE, TRUE, 0);
    g_signal_connect(app_data.brightness_offset_scale, "value-changed",
                     G_CALLBACK(on_brightness_offset_changed), NULL);

    app_data.brightness_offset_label = gtk_label_new("0%");
    gtk_widget_set_size_request(app_data.brightness_offset_label, 50, -1);
    gtk_box_pack_start(GTK_BOX(offset_hbox), app_data.brightness_offset_label, FALSE, FALSE, 0);

    /* Disable main display option if no laptop backlight available */
    if (!laptop_backlight_is_available(app_data.laptop_backlight)) {
        gtk_widget_set_sensitive(app_data.auto_brightness_laptop_radio, FALSE);
        gtk_widget_set_sensitive(offset_hbox, FALSE);
        gtk_widget_set_tooltip_text(app_data.auto_brightness_laptop_radio,
                                   "No main display backlight detected on this system");
    }

    /* Startup options frame */
    GtkWidget *startup_frame = gtk_frame_new("Options");
    gtk_box_pack_start(GTK_BOX(main_vbox), startup_frame, FALSE, FALSE, 0);
    
    GtkWidget *startup_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(startup_vbox), 10);
    gtk_container_add(GTK_CONTAINER(startup_frame), startup_vbox);
    
    app_data.start_minimized_check = gtk_check_button_new_with_label("Start minimized to system tray");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.start_minimized_check), 
                                config_get_start_minimized(app_data.config));
    gtk_box_pack_start(GTK_BOX(startup_vbox), app_data.start_minimized_check, FALSE, FALSE, 0);
    g_signal_connect(app_data.start_minimized_check, "toggled", 
                     G_CALLBACK(on_start_minimized_toggled), NULL);
    
    app_data.show_brightness_tray_check = gtk_check_button_new_with_label("Show brightness level in tray icon");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.show_brightness_tray_check),
                                config_get_show_brightness_in_tray(app_data.config));
    gtk_box_pack_start(GTK_BOX(startup_vbox), app_data.show_brightness_tray_check, FALSE, FALSE, 0);
    g_signal_connect(app_data.show_brightness_tray_check, "toggled",
                     G_CALLBACK(on_show_brightness_tray_toggled), NULL);

    app_data.show_light_level_tray_check = gtk_check_button_new_with_label("Show ambient light level in tray icon");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.show_light_level_tray_check),
                                config_get_show_light_level_in_tray(app_data.config));
    gtk_box_pack_start(GTK_BOX(startup_vbox), app_data.show_light_level_tray_check, FALSE, FALSE, 0);
    g_signal_connect(app_data.show_light_level_tray_check, "toggled",
                     G_CALLBACK(on_show_light_level_tray_toggled), NULL);

    /* Disable light level option if no sensor available */
    if (!light_sensor_is_available(app_data.light_sensor)) {
        gtk_widget_set_sensitive(app_data.show_light_level_tray_check, FALSE);
        gtk_widget_set_tooltip_text(app_data.show_light_level_tray_check,
                                   "No ambient light sensor detected on this system");
    }

    /* Button frame */
    button_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(button_frame), GTK_SHADOW_NONE);
    gtk_box_pack_start(GTK_BOX(main_vbox), button_frame, FALSE, FALSE, 0);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(button_frame), hbox);
    
    refresh_button = gtk_button_new_with_label("Refresh Monitors");
    gtk_box_pack_start(GTK_BOX(hbox), refresh_button, FALSE, FALSE, 0);
    g_signal_connect(refresh_button, "clicked", 
                     G_CALLBACK(on_refresh_monitors_clicked), NULL);
    
    quit_button = gtk_button_new_with_label("Quit");
    gtk_box_pack_end(GTK_BOX(hbox), quit_button, FALSE, FALSE, 0);
    g_signal_connect(quit_button, "clicked", 
                     G_CALLBACK(on_window_destroy), NULL);
}

/* Load available monitors */
static void load_monitors(void)
{
    /* Clear existing monitors */
    if (app_data.monitors) {
        monitor_list_free(app_data.monitors);
        app_data.monitors = NULL;
        app_data.current_monitor = NULL;
    }
    
    /* Clear combo box */
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(app_data.monitor_combo));
    if (model) {
        gtk_list_store_clear(GTK_LIST_STORE(model));
    }
    
    /* Detect monitors */
    app_data.monitors = monitor_detect_all();
    
    if (!app_data.monitors || monitor_list_get_count(app_data.monitors) == 0) {
        app_data.monitors_found = FALSE;
        
        /* Start retry timer if this is the initial load (retry_attempt == 0) */
        if (app_data.monitor_retry_attempt == 0) {
            app_data.monitor_retry_attempt = 1;
            app_data.monitor_retry_timer = g_timeout_add_seconds(30, load_monitors_with_retry, NULL);
            g_message("No monitors found on startup, will retry in 30 seconds...");
        }
        
#if HAVE_APPINDICATOR
        /* Update tray icon to reflect no monitors state */
        update_tray_icon_label();
#endif
        return;
    }
    
    /* Monitors found! */
    app_data.monitors_found = TRUE;
    
    /* Cancel any pending retry timer */
    if (app_data.monitor_retry_timer > 0) {
        g_source_remove(app_data.monitor_retry_timer);
        app_data.monitor_retry_timer = 0;
        app_data.monitor_retry_attempt = 0;
        g_message("Monitors detected successfully!");
    }
    
    /* Populate combo box */
    const char *default_monitor = config_get_default_monitor(app_data.config);
    int default_index = -1;
    
    for (int i = 0; i < monitor_list_get_count(app_data.monitors); i++) {
        Monitor *monitor = monitor_list_get_monitor(app_data.monitors, i);
        const char *display_name = monitor_get_display_name(monitor);
        const char *device_path = monitor_get_device_path(monitor);
        
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app_data.monitor_combo), display_name);
        
        if (default_monitor && strcmp(device_path, default_monitor) == 0) {
            default_index = i;
        }
    }
    
    /* Select default monitor */
    if (default_index >= 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app_data.monitor_combo), default_index);
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app_data.monitor_combo), 0);
    }
    
#if HAVE_APPINDICATOR
    /* Update tray icon to reflect monitors found */
    update_tray_icon_label();
#endif
}

/* Retry monitor detection with delayed intervals */
static gboolean load_monitors_with_retry(gpointer data)
{
    (void)data;
    
    /* Clear the timer ID since it's about to complete */
    app_data.monitor_retry_timer = 0;
    
    /* Try to detect monitors again */
    g_message("Retrying monitor detection (attempt %d)...", app_data.monitor_retry_attempt);
    
    /* Temporarily increase retry attempt to avoid triggering another retry from load_monitors */
    int saved_attempt = app_data.monitor_retry_attempt;
    app_data.monitor_retry_attempt = -1;  /* Special value to indicate retry in progress */
    
    /* Clear existing monitors */
    if (app_data.monitors) {
        monitor_list_free(app_data.monitors);
        app_data.monitors = NULL;
        app_data.current_monitor = NULL;
    }
    
    /* Clear combo box */
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(app_data.monitor_combo));
    if (model) {
        gtk_list_store_clear(GTK_LIST_STORE(model));
    }
    
    /* Detect monitors */
    app_data.monitors = monitor_detect_all();
    
    if (!app_data.monitors || monitor_list_get_count(app_data.monitors) == 0) {
        /* Restore attempt counter */
        app_data.monitor_retry_attempt = saved_attempt;
        
        /* Schedule next retry based on attempt number */
        if (app_data.monitor_retry_attempt == 1) {
            /* Second attempt: retry after 90 seconds from startup (60 more seconds) */
            app_data.monitor_retry_attempt = 2;
            app_data.monitor_retry_timer = g_timeout_add_seconds(60, load_monitors_with_retry, NULL);
            g_message("No monitors found on retry 1, will retry in 60 seconds...");
        } else if (app_data.monitor_retry_attempt == 2) {
            /* Third attempt: retry after 180 seconds from startup (90 more seconds) */
            app_data.monitor_retry_attempt = 3;
            app_data.monitor_retry_timer = g_timeout_add_seconds(90, load_monitors_with_retry, NULL);
            g_message("No monitors found on retry 2, will retry in 90 seconds...");
        } else {
            /* Final attempt failed, stop retrying */
            app_data.monitor_retry_attempt = 0;
            app_data.monitors_found = FALSE;
            g_message("All monitor detection attempts failed");
            
#if HAVE_APPINDICATOR
            /* Update tray icon to reflect no monitors state */
            update_tray_icon_label();
#endif
        }
        
        return FALSE; /* Stop the current timer */
    }
    
    /* Monitors found! */
    app_data.monitors_found = TRUE;
    app_data.monitor_retry_attempt = 0;
    g_message("Monitors detected successfully on retry!");
    
    /* Populate combo box */
    const char *default_monitor = config_get_default_monitor(app_data.config);
    int default_index = -1;
    
    for (int i = 0; i < monitor_list_get_count(app_data.monitors); i++) {
        Monitor *monitor = monitor_list_get_monitor(app_data.monitors, i);
        const char *display_name = monitor_get_display_name(monitor);
        const char *device_path = monitor_get_device_path(monitor);
        
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app_data.monitor_combo), display_name);
        
        if (default_monitor && strcmp(device_path, default_monitor) == 0) {
            default_index = i;
        }
    }
    
    /* Select default monitor */
    if (default_index >= 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app_data.monitor_combo), default_index);
    } else {
        gtk_combo_box_set_active(GTK_COMBO_BOX(app_data.monitor_combo), 0);
    }
    
#if HAVE_APPINDICATOR
    /* Update tray icon to reflect monitors found */
    update_tray_icon_label();
#endif
    
    return FALSE; /* Stop the timer */
}

/* Immediately re-check monitor availability (for hardware disconnect events) */
static gboolean recheck_monitors_immediately(gpointer data)
{
    (void)data;
    
    g_message("Re-checking monitor availability immediately");
    
    /* Save current state to compare after re-detection */
    gboolean had_monitors = app_data.monitors_found;
    
    /* Re-run monitor detection */
    load_monitors();
    
    /* If we had monitors before but don't now, update the UI appropriately */
    if (had_monitors && !app_data.monitors_found) {
        g_message("Monitor disconnection detected - updating UI");
        
        /* Clear current monitor selection */
        app_data.current_monitor = NULL;
        
        /* Clear combo box */
        GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(app_data.monitor_combo));
        if (model) {
            gtk_list_store_clear(GTK_LIST_STORE(model));
        }
        
        /* Reset brightness display */
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 50);
        update_brightness_display();
        
#if HAVE_APPINDICATOR
        /* Update tray icon to show "X" */
        update_tray_icon_label();
#endif
    }
    
    return FALSE; /* Single execution */
}

/* Auto-refresh monitors when DDC communication fails */
static gboolean auto_refresh_monitors_on_failure(void)
{
    g_message("DDC communication failed, auto-refreshing monitors...");

    /* Set flag to prevent recursion during refresh */
    app_data.in_monitor_refresh = TRUE;

    /* Save current state */
    gboolean had_monitors = app_data.monitors_found;
    const char *current_device_path = NULL;
    if (app_data.current_monitor) {
        current_device_path = monitor_get_device_path(app_data.current_monitor);
    }

    /* Refresh monitors */
    load_monitors();

    /* Try to restore the same monitor selection if possible */
    if (current_device_path && app_data.monitors && monitor_list_get_count(app_data.monitors) > 0) {
        for (int i = 0; i < monitor_list_get_count(app_data.monitors); i++) {
            Monitor *monitor = monitor_list_get_monitor(app_data.monitors, i);
            if (strcmp(monitor_get_device_path(monitor), current_device_path) == 0) {
                app_data.current_monitor = monitor_list_get_monitor(app_data.monitors, i);
                gtk_combo_box_set_active(GTK_COMBO_BOX(app_data.monitor_combo), i);
                break;
            }
        }
    }

    /* Clear refresh flag */
    app_data.in_monitor_refresh = FALSE;

    /* Check if we successfully found monitors after refresh */
    if (app_data.monitors_found) {
        g_message("Monitor refresh successful");
        return TRUE;
    } else {
        g_message("Monitor refresh failed - no monitors found");

        /* Update UI to reflect no monitors available */
        app_data.current_monitor = NULL;

        /* Clear combo box */
        GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(app_data.monitor_combo));
        if (model) {
            gtk_list_store_clear(GTK_LIST_STORE(model));
        }

        /* Reset brightness display */
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 50);
        update_brightness_display();

#if HAVE_APPINDICATOR
        /* Update tray icon to show "X" */
        update_tray_icon_label();
#endif

        return FALSE;
    }
}

/* Update brightness percentage display */
static void update_brightness_display(void)
{
    int brightness = (int)gtk_range_get_value(GTK_RANGE(app_data.brightness_scale));
    char text[16];
    snprintf(text, sizeof(text), "%d%%", brightness);
    gtk_label_set_text(GTK_LABEL(app_data.brightness_label), text);
    
#if HAVE_APPINDICATOR
    /* Update tray icon and menu */
    update_tray_icon_label();
    update_indicator_menu();
#endif
}

/* Window delete event handler */
static gboolean on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)widget; (void)event; (void)data;
    
#if HAVE_APPINDICATOR
    /* Hide to tray instead of closing if tray is available */
    gtk_widget_hide(app_data.main_window);
    return TRUE; /* Don't propagate the event */
#else
    /* No tray available, quit the application */
    gtk_main_quit();
    return FALSE;
#endif
}

/* Start minimized checkbox toggled */
static void on_start_minimized_toggled(GtkToggleButton *button, gpointer data)
{
    (void)data;
    gboolean enabled = gtk_toggle_button_get_active(button);
    config_set_start_minimized(app_data.config, enabled);
    config_save(app_data.config);
}

/* Show brightness in tray checkbox toggled */
static void on_show_brightness_tray_toggled(GtkToggleButton *button, gpointer data)
{
    (void)data;
    gboolean enabled = gtk_toggle_button_get_active(button);
    config_set_show_brightness_in_tray(app_data.config, enabled);
    config_save(app_data.config);

#if HAVE_APPINDICATOR
    /* Update tray icon label immediately */
    update_tray_icon_label();
#endif
}

/* Show light level in tray checkbox toggled */
static void on_show_light_level_tray_toggled(GtkToggleButton *button, gpointer data)
{
    (void)data;
    gboolean enabled = gtk_toggle_button_get_active(button);
    config_set_show_light_level_in_tray(app_data.config, enabled);
    config_save(app_data.config);

#if HAVE_APPINDICATOR
    /* Update tray icon label immediately */
    update_tray_icon_label();
#endif
}

#if HAVE_APPINDICATOR

/* Setup tray indicator */
static void setup_tray_indicator(void)
{
    /* Create indicator */
    app_data.indicator = app_indicator_new("ddc-automatic-brightness",
                                          "brightness-control", 
                                          APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    
    /* Set icon - try multiple locations and sizes */
    char *base_paths[] = {
        "/usr/local/share/pixmaps/ddc-automatic-brightness-icon",
        g_build_filename(g_get_home_dir(), ".local/share/pixmaps/ddc-automatic-brightness-icon", NULL),
        g_build_filename(g_get_current_dir(), "ddc-automatic-brightness-icon", NULL),
        g_build_filename(g_get_current_dir(), "..", "ddc-automatic-brightness-icon", NULL),
        NULL
    };
    
    /* Try different icon sizes for better tray compatibility */
    const char *sizes[] = {"", "-24", "-22", "-32", "-16", NULL};
    
    gboolean icon_set = FALSE;
    for (int i = 0; base_paths[i] != NULL && !icon_set; i++) {
        for (int j = 0; sizes[j] != NULL && !icon_set; j++) {
            char *full_path = g_strdup_printf("%s%s.png", base_paths[i], sizes[j]);
            if (g_file_test(full_path, G_FILE_TEST_EXISTS)) {
                g_message("Using icon from: %s", full_path);
                app_indicator_set_icon_full(app_data.indicator, full_path, "DDC Brightness");
                icon_set = TRUE;
            }
            g_free(full_path);
        }
    }
    
    if (!icon_set) {
        g_message("Icon file not found, using theme icon");
        /* Fallback to theme icons */
        if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "display-brightness-symbolic")) {
            app_indicator_set_icon(app_data.indicator, "display-brightness-symbolic");
        } else if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "brightness-control")) {
            app_indicator_set_icon(app_data.indicator, "brightness-control");
        } else {
            app_indicator_set_icon(app_data.indicator, "display");
        }
    }
    
    /* Free dynamically allocated paths */
    for (int i = 1; base_paths[i] != NULL && i <= 3; i++) {
        g_free(base_paths[i]);
    }
    
    /* Set status */
    app_indicator_set_status(app_data.indicator, APP_INDICATOR_STATUS_ACTIVE);
    
    /* Create menu */
    app_data.indicator_menu = gtk_menu_new();
    
    /* Brightness submenu */
    GtkWidget *brightness_item = gtk_menu_item_new_with_label("Brightness");
    GtkWidget *brightness_submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(brightness_item), brightness_submenu);
    
    GtkWidget *brightness_20 = gtk_menu_item_new_with_label("20%");
    GtkWidget *brightness_25 = gtk_menu_item_new_with_label("25%");
    GtkWidget *brightness_35 = gtk_menu_item_new_with_label("35%");
    GtkWidget *brightness_50 = gtk_menu_item_new_with_label("50%");
    GtkWidget *brightness_70 = gtk_menu_item_new_with_label("70%");
    GtkWidget *brightness_100 = gtk_menu_item_new_with_label("100%");
    
    gtk_menu_shell_append(GTK_MENU_SHELL(brightness_submenu), brightness_20);
    gtk_menu_shell_append(GTK_MENU_SHELL(brightness_submenu), brightness_25);
    gtk_menu_shell_append(GTK_MENU_SHELL(brightness_submenu), brightness_35);
    gtk_menu_shell_append(GTK_MENU_SHELL(brightness_submenu), brightness_50);
    gtk_menu_shell_append(GTK_MENU_SHELL(brightness_submenu), brightness_70);
    gtk_menu_shell_append(GTK_MENU_SHELL(brightness_submenu), brightness_100);
    
    g_signal_connect(brightness_20, "activate", G_CALLBACK(on_indicator_brightness_20), NULL);
    g_signal_connect(brightness_25, "activate", G_CALLBACK(on_indicator_brightness_25), NULL);
    g_signal_connect(brightness_35, "activate", G_CALLBACK(on_indicator_brightness_35), NULL);
    g_signal_connect(brightness_50, "activate", G_CALLBACK(on_indicator_brightness_50), NULL);
    g_signal_connect(brightness_70, "activate", G_CALLBACK(on_indicator_brightness_70), NULL);
    g_signal_connect(brightness_100, "activate", G_CALLBACK(on_indicator_brightness_100), NULL);
    
    /* Auto brightness section header (non-interactive label) */
    GtkWidget *auto_brightness_label = gtk_menu_item_new_with_label("Auto Brightness:");
    gtk_widget_set_sensitive(auto_brightness_label, FALSE);  /* Make it non-clickable */

    /* Auto brightness options (always visible, no submenu) */
    GtkWidget *auto_schedule = gtk_check_menu_item_new_with_label("  Time-based schedule");
    GtkWidget *auto_sensor = gtk_check_menu_item_new_with_label("  Ambient light sensor");
    GtkWidget *auto_main_display = gtk_check_menu_item_new_with_label("  Follow main display");

    g_signal_connect(auto_schedule, "activate", G_CALLBACK(on_indicator_auto_schedule), NULL);
    g_signal_connect(auto_sensor, "activate", G_CALLBACK(on_indicator_auto_sensor), NULL);
    g_signal_connect(auto_main_display, "activate", G_CALLBACK(on_indicator_auto_main_display), NULL);

    /* Disable sensor option if not available */
    if (!light_sensor_is_available(app_data.light_sensor)) {
        gtk_widget_set_sensitive(auto_sensor, FALSE);
    }

    /* Disable main display option if not available */
    if (!laptop_backlight_is_available(app_data.laptop_backlight)) {
        gtk_widget_set_sensitive(auto_main_display, FALSE);
    }

    /* Separator */
    GtkWidget *separator1 = gtk_separator_menu_item_new();

    /* Show window */
    GtkWidget *show_item = gtk_menu_item_new_with_label("Show Window");
    g_signal_connect(show_item, "activate", G_CALLBACK(on_indicator_show_window), NULL);

    /* Separator */
    GtkWidget *separator2 = gtk_separator_menu_item_new();

    /* Quit */
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_indicator_quit), NULL);

    /* Add items to menu */
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), brightness_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), auto_brightness_label);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), auto_schedule);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), auto_sensor);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), auto_main_display);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), separator1);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), show_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), separator2);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), quit_item);
    
    /* Connect signal to update menu when it's shown */
    g_signal_connect(app_data.indicator_menu, "show", G_CALLBACK(on_indicator_menu_show), NULL);

    /* Show all menu items */
    gtk_widget_show_all(app_data.indicator_menu);

    /* Set menu */
    app_indicator_set_menu(app_data.indicator, GTK_MENU(app_data.indicator_menu));
}

/* Indicator brightness callbacks */
static void on_indicator_brightness_20(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        /* Disable auto brightness and save config immediately */
        config_set_monitor_auto_brightness_mode(app_data.config,
                                                monitor_get_device_path(app_data.current_monitor),
                                                AUTO_BRIGHTNESS_MODE_DISABLED);

        monitor_set_brightness_with_retry(app_data.current_monitor, 20, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 20);
        app_data.updating_from_auto = FALSE;

        /* Update UI - this will now reflect the disabled mode in the menu */
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio), TRUE);
        }

        update_brightness_display();
    }
}

static void on_indicator_brightness_25(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        /* Disable auto brightness and save config immediately */
        config_set_monitor_auto_brightness_mode(app_data.config,
                                                monitor_get_device_path(app_data.current_monitor),
                                                AUTO_BRIGHTNESS_MODE_DISABLED);

        monitor_set_brightness_with_retry(app_data.current_monitor, 25, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 25);
        app_data.updating_from_auto = FALSE;

        /* Update UI - this will now reflect the disabled mode in the menu */
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio), TRUE);
        }

        update_brightness_display();
    }
}

static void on_indicator_brightness_50(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        /* Disable auto brightness and save config immediately */
        config_set_monitor_auto_brightness_mode(app_data.config,
                                                monitor_get_device_path(app_data.current_monitor),
                                                AUTO_BRIGHTNESS_MODE_DISABLED);

        monitor_set_brightness_with_retry(app_data.current_monitor, 50, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 50);
        app_data.updating_from_auto = FALSE;

        /* Update UI - this will now reflect the disabled mode in the menu */
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio), TRUE);
        }

        update_brightness_display();
    }
}

static void on_indicator_brightness_35(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        /* Disable auto brightness and save config immediately */
        config_set_monitor_auto_brightness_mode(app_data.config,
                                                monitor_get_device_path(app_data.current_monitor),
                                                AUTO_BRIGHTNESS_MODE_DISABLED);

        monitor_set_brightness_with_retry(app_data.current_monitor, 35, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 35);
        app_data.updating_from_auto = FALSE;

        /* Update UI - this will now reflect the disabled mode in the menu */
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio), TRUE);
        }

        update_brightness_display();
    }
}

static void on_indicator_brightness_70(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        /* Disable auto brightness and save config immediately */
        config_set_monitor_auto_brightness_mode(app_data.config,
                                                monitor_get_device_path(app_data.current_monitor),
                                                AUTO_BRIGHTNESS_MODE_DISABLED);

        monitor_set_brightness_with_retry(app_data.current_monitor, 70, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 70);
        app_data.updating_from_auto = FALSE;

        /* Update UI - this will now reflect the disabled mode in the menu */
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio), TRUE);
        }

        update_brightness_display();
    }
}

static void on_indicator_brightness_100(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        /* Disable auto brightness and save config immediately */
        config_set_monitor_auto_brightness_mode(app_data.config,
                                                monitor_get_device_path(app_data.current_monitor),
                                                AUTO_BRIGHTNESS_MODE_DISABLED);

        monitor_set_brightness_with_retry(app_data.current_monitor, 100, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 100);
        app_data.updating_from_auto = FALSE;

        /* Update UI - this will now reflect the disabled mode in the menu */
        if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_disabled_radio), TRUE);
        }

        update_brightness_display();
    }
}

static void on_indicator_auto_schedule(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_schedule_radio), TRUE);
    }
}

static void on_indicator_auto_sensor(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor && light_sensor_is_available(app_data.light_sensor)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_sensor_radio), TRUE);
    }
}

static void on_indicator_auto_main_display(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor && laptop_backlight_is_available(app_data.laptop_backlight)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_laptop_radio), TRUE);
    }
}

static void on_indicator_show_window(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    gtk_widget_show_all(app_data.main_window);
    gtk_window_present(GTK_WINDOW(app_data.main_window));
}

static void on_indicator_quit(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    gtk_main_quit();
}

static void update_tray_icon_label(void)
{
    if (!app_data.indicator) {
        return;
    }

    /* Check if monitors are available */
    if (!app_data.monitors_found || !app_data.current_monitor || !monitor_is_available(app_data.current_monitor)) {
        /* Show "X" to indicate no monitors found or current monitor unavailable */
        app_indicator_set_label(app_data.indicator, "X", "X");
        return;
    }

    gboolean show_brightness = config_get_show_brightness_in_tray(app_data.config);
    gboolean show_light_level = config_get_show_light_level_in_tray(app_data.config);

    /* Get actual current brightness from monitor (what was last sent to hardware) */
    int brightness = monitor_get_current_brightness(app_data.current_monitor);
    if (brightness < 0) {
        /* If current brightness unknown, fall back to slider value */
        brightness = (int)gtk_range_get_value(GTK_RANGE(app_data.brightness_scale));
    }

    /* Build label based on what's enabled */
    if (show_brightness && show_light_level && light_sensor_is_available(app_data.light_sensor)) {
        /* Show both brightness and light level */
        double lux = light_sensor_read_lux(app_data.light_sensor);

        if (lux >= 0) {
            char label[48];
            char lux_str[24];

            /* Format lux value */
            if (lux < 10) {
                snprintf(lux_str, sizeof(lux_str), "%.1f lx", lux);
            } else if (lux < 100) {
                snprintf(lux_str, sizeof(lux_str), "%.0f lx", lux);
            } else if (lux < 1000) {
                snprintf(lux_str, sizeof(lux_str), "%.0f lx", lux);
            } else {
                /* For values >= 1000, show in k (thousands) */
                snprintf(lux_str, sizeof(lux_str), "%.1fk lx", lux / 1000.0);
            }

            /* Combine both values */
            snprintf(label, sizeof(label), "%d%% | %s", brightness, lux_str);
            app_indicator_set_label(app_data.indicator, label, "100% | 9999 lx");
        } else {
            /* Sensor read failed, show only brightness */
            char label[16];
            snprintf(label, sizeof(label), "%d%%", brightness);
            app_indicator_set_label(app_data.indicator, label, "100%");
        }
    } else if (show_light_level && light_sensor_is_available(app_data.light_sensor)) {
        /* Show only ambient light level */
        double lux = light_sensor_read_lux(app_data.light_sensor);
        if (lux >= 0) {
            char label[32];
            if (lux < 10) {
                snprintf(label, sizeof(label), "%.1f lx", lux);
            } else if (lux < 100) {
                snprintf(label, sizeof(label), "%.0f lx", lux);
            } else if (lux < 1000) {
                snprintf(label, sizeof(label), "%.0f lx", lux);
            } else {
                /* For values >= 1000, show in k (thousands) */
                snprintf(label, sizeof(label), "%.1fk lx", lux / 1000.0);
            }
            app_indicator_set_label(app_data.indicator, label, "9999 lx");
        } else {
            /* Sensor read failed, clear label */
            app_indicator_set_label(app_data.indicator, "", "");
        }
    } else if (show_brightness && app_data.current_monitor) {
        /* Show only brightness percentage (actual current brightness) */
        char label[16];
        snprintf(label, sizeof(label), "%d%%", brightness);
        app_indicator_set_label(app_data.indicator, label, "100%");
    } else {
        /* Clear label */
        app_indicator_set_label(app_data.indicator, "", "");
    }
}

/* Callback when tray menu is shown - update menu items */
static void on_indicator_menu_show(GtkWidget *menu, gpointer data)
{
    (void)menu;
    (void)data;
    update_indicator_menu();
}

static void update_indicator_menu(void)
{
    gboolean show_brightness_in_tray = config_get_show_brightness_in_tray(app_data.config);

    /* Get current auto brightness mode */
    AutoBrightnessMode mode = AUTO_BRIGHTNESS_MODE_DISABLED;
    if (app_data.current_monitor) {
        mode = config_get_monitor_auto_brightness_mode(app_data.config,
                                                       monitor_get_device_path(app_data.current_monitor));
    }

    /* Calculate brightness for each mode */
    int schedule_brightness = scheduler_get_current_brightness(app_data.scheduler);
    int sensor_brightness = -1;
    int main_display_brightness = -1;

    if (light_sensor_is_available(app_data.light_sensor) && app_data.current_monitor) {
        load_light_sensor_curve_for_monitor(monitor_get_device_path(app_data.current_monitor));
        double lux = light_sensor_read_lux(app_data.light_sensor);
        if (lux >= 0) {
            sensor_brightness = light_sensor_calculate_brightness(app_data.light_sensor, lux);
        }
    }

    if (laptop_backlight_is_available(app_data.laptop_backlight) && app_data.current_monitor) {
        int laptop_brightness = laptop_backlight_read_brightness(app_data.laptop_backlight);
        if (laptop_brightness >= 0) {
            /* Apply brightness offset to get the actual target brightness */
            int offset = config_get_monitor_brightness_offset(app_data.config,
                                                              monitor_get_device_path(app_data.current_monitor));
            main_display_brightness = laptop_brightness + offset;

            /* Clamp to 0-100 range */
            if (main_display_brightness < 0) main_display_brightness = 0;
            if (main_display_brightness > 100) main_display_brightness = 100;
        }
    }

    /* Update menu items */
    GList *children = gtk_container_get_children(GTK_CONTAINER(app_data.indicator_menu));
    for (GList *iter = children; iter; iter = iter->next) {
        GtkWidget *child = GTK_WIDGET(iter->data);

        if (GTK_IS_MENU_ITEM(child)) {
            const char *label = gtk_menu_item_get_label(GTK_MENU_ITEM(child));

            /* Update Brightness submenu label */
            if (label && strncmp(label, "Brightness", 10) == 0) {
                if (!show_brightness_in_tray && app_data.current_monitor) {
                    int brightness = (int)gtk_range_get_value(GTK_RANGE(app_data.brightness_scale));
                    char new_label[32];
                    snprintf(new_label, sizeof(new_label), "Brightness: %d%%", brightness);
                    gtk_menu_item_set_label(GTK_MENU_ITEM(child), new_label);
                } else {
                    gtk_menu_item_set_label(GTK_MENU_ITEM(child), "Brightness");
                }
            }

            /* Update Auto Brightness items (flat in main menu, no submenu) */
            if (GTK_IS_CHECK_MENU_ITEM(child) && label) {
                char new_label[64];
                gboolean is_active = FALSE;

                if (strncmp(label, "  Time-based", 12) == 0) {
                    is_active = (mode == AUTO_BRIGHTNESS_MODE_TIME_SCHEDULE);
                    if (schedule_brightness >= 0) {
                        snprintf(new_label, sizeof(new_label), "  Time-based schedule (%d%%)", schedule_brightness);
                    } else {
                        snprintf(new_label, sizeof(new_label), "  Time-based schedule");
                    }
                } else if (strncmp(label, "  Ambient", 9) == 0) {
                    is_active = (mode == AUTO_BRIGHTNESS_MODE_LIGHT_SENSOR);
                    if (sensor_brightness >= 0) {
                        snprintf(new_label, sizeof(new_label), "  Ambient light sensor (%d%%)", sensor_brightness);
                    } else {
                        snprintf(new_label, sizeof(new_label), "  Ambient light sensor");
                    }
                } else if (strncmp(label, "  Follow", 8) == 0) {
                    is_active = (mode == AUTO_BRIGHTNESS_MODE_LAPTOP_DISPLAY);
                    if (main_display_brightness >= 0) {
                        snprintf(new_label, sizeof(new_label), "  Follow main display (%d%%)", main_display_brightness);
                    } else {
                        snprintf(new_label, sizeof(new_label), "  Follow main display");
                    }
                } else {
                    continue;
                }

                /* Block signals to prevent triggering the callback */
                g_signal_handlers_block_by_func(child, G_CALLBACK(on_indicator_auto_schedule), NULL);
                g_signal_handlers_block_by_func(child, G_CALLBACK(on_indicator_auto_sensor), NULL);
                g_signal_handlers_block_by_func(child, G_CALLBACK(on_indicator_auto_main_display), NULL);

                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(child), is_active);

                /* Unblock signals */
                g_signal_handlers_unblock_by_func(child, G_CALLBACK(on_indicator_auto_schedule), NULL);
                g_signal_handlers_unblock_by_func(child, G_CALLBACK(on_indicator_auto_sensor), NULL);
                g_signal_handlers_unblock_by_func(child, G_CALLBACK(on_indicator_auto_main_display), NULL);

                gtk_menu_item_set_label(GTK_MENU_ITEM(child), new_label);
            }
        }
    }
    g_list_free(children);
}

#endif

#if HAVE_LIBUDEV
/* Setup udev monitoring for hardware changes */
static gboolean setup_udev_monitoring(void)
{
    app_data.udev = udev_new();
    if (!app_data.udev) {
        g_warning("Cannot create udev context");
        return FALSE;
    }
    
    app_data.udev_monitor = udev_monitor_new_from_netlink(app_data.udev, "udev");
    if (!app_data.udev_monitor) {
        g_warning("Cannot create udev monitor");
        udev_unref(app_data.udev);
        app_data.udev = NULL;
        return FALSE;
    }
    
    /* Monitor USB and DRM (display) subsystems for relevant hardware changes */
    udev_monitor_filter_add_match_subsystem_devtype(app_data.udev_monitor, "usb", NULL);
    udev_monitor_filter_add_match_subsystem_devtype(app_data.udev_monitor, "drm", NULL);
    udev_monitor_filter_add_match_subsystem_devtype(app_data.udev_monitor, "i2c", NULL);
    
    if (udev_monitor_enable_receiving(app_data.udev_monitor) < 0) {
        g_warning("Cannot enable udev monitor");
        udev_monitor_unref(app_data.udev_monitor);
        udev_unref(app_data.udev);
        app_data.udev_monitor = NULL;
        app_data.udev = NULL;
        return FALSE;
    }
    
    /* Create GIO channel to monitor udev events */
    int fd = udev_monitor_get_fd(app_data.udev_monitor);
    app_data.udev_io_channel = g_io_channel_unix_new(fd);
    
    /* Set up the watch */
    app_data.udev_watch_id = g_io_add_watch(app_data.udev_io_channel, 
                                           G_IO_IN, 
                                           on_udev_event, 
                                           NULL);
    
    g_message("Udev monitoring setup successfully");
    return TRUE;
}

/* Cleanup udev monitoring */
static void cleanup_udev_monitoring(void)
{
    if (app_data.udev_watch_id > 0) {
        g_source_remove(app_data.udev_watch_id);
        app_data.udev_watch_id = 0;
    }
    
    if (app_data.udev_io_channel) {
        g_io_channel_unref(app_data.udev_io_channel);
        app_data.udev_io_channel = NULL;
    }
    
    if (app_data.udev_monitor) {
        udev_monitor_unref(app_data.udev_monitor);
        app_data.udev_monitor = NULL;
    }
    
    if (app_data.udev) {
        udev_unref(app_data.udev);
        app_data.udev = NULL;
    }
}

/* Handle udev events */
static gboolean on_udev_event(GIOChannel *channel, GIOCondition condition, gpointer data)
{
    (void)channel; (void)data;
    
    if (condition & G_IO_IN) {
        struct udev_device *device = udev_monitor_receive_device(app_data.udev_monitor);
        if (device) {
            const char *action = udev_device_get_action(device);
            const char *subsystem = udev_device_get_subsystem(device);
            /* const char *devtype = udev_device_get_devtype(device); */ /* Not currently used */
            
            /* Check for device addition/removal events that might affect display hardware */
            if (action && (strcmp(action, "add") == 0 || strcmp(action, "remove") == 0)) {
                gboolean should_check_monitors = FALSE;
                
                if (subsystem && strcmp(subsystem, "drm") == 0) {
                    /* DRM device added/removed - could be display hardware */
                    should_check_monitors = TRUE;
                    g_message("DRM device %s, checking monitor status", action);
                } else if (subsystem && strcmp(subsystem, "usb") == 0) {
                    /* USB device added/removed - could be USB-C/Thunderbolt display */
                    should_check_monitors = TRUE;
                    g_message("USB device %s, checking monitor status", action);
                } else if (subsystem && strcmp(subsystem, "i2c") == 0) {
                    /* I2C device added/removed - DDC/CI uses I2C */
                    should_check_monitors = TRUE;
                    g_message("I2C device %s, checking monitor status", action);
                }
                
                if (should_check_monitors) {
                    /* Cancel any existing retry timer */
                    if (app_data.monitor_retry_timer > 0) {
                        g_source_remove(app_data.monitor_retry_timer);
                        app_data.monitor_retry_timer = 0;
                    }
                    
                    if (strcmp(action, "add") == 0) {
                        /* Device added - retry detection if we don't have monitors */
                        if (!app_data.monitors_found) {
                            app_data.monitor_retry_attempt = 1;
                            app_data.monitor_retry_timer = g_timeout_add_seconds(2, load_monitors_with_retry, NULL);
                            g_message("Hardware added, will retry monitor detection in 2 seconds");
                        }
                    } else if (strcmp(action, "remove") == 0) {
                        /* Device removed - immediately re-check to see if our monitor was disconnected */
                        g_timeout_add_seconds(1, recheck_monitors_immediately, NULL);
                        g_message("Hardware removed, will re-check monitor status in 1 second");
                    }
                }
            }
            
            udev_device_unref(device);
        }
    }
    
    return TRUE; /* Keep the watch active */
}
#endif /* HAVE_LIBUDEV */

/* Setup inotify monitoring for laptop backlight changes */
static gboolean setup_laptop_backlight_monitoring(void)
{
    /* Initialize fields */
    app_data.laptop_backlight_inotify_fd = -1;
    app_data.laptop_backlight_watch_fd = -1;
    app_data.laptop_backlight_io_channel = NULL;
    app_data.laptop_backlight_watch_id = 0;
    app_data.last_laptop_brightness = -1;

    /* Only setup monitoring if laptop backlight is available */
    if (!laptop_backlight_is_available(app_data.laptop_backlight)) {
        return FALSE;
    }

    /* Get the backlight device path */
    const char *device_path = laptop_backlight_get_device_path(app_data.laptop_backlight);
    if (!device_path) {
        g_warning("Cannot get laptop backlight device path");
        return FALSE;
    }

    /* Build the path to the brightness file */
    char brightness_file[512];
    snprintf(brightness_file, sizeof(brightness_file), "%s/brightness", device_path);

    /* Create inotify instance */
    app_data.laptop_backlight_inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (app_data.laptop_backlight_inotify_fd < 0) {
        g_warning("Failed to create inotify instance for laptop backlight: %s", strerror(errno));
        return FALSE;
    }

    /* Watch the brightness file for modifications */
    app_data.laptop_backlight_watch_fd = inotify_add_watch(app_data.laptop_backlight_inotify_fd,
                                                            brightness_file,
                                                            IN_MODIFY);
    if (app_data.laptop_backlight_watch_fd < 0) {
        g_warning("Failed to watch laptop backlight file %s: %s", brightness_file, strerror(errno));
        close(app_data.laptop_backlight_inotify_fd);
        app_data.laptop_backlight_inotify_fd = -1;
        return FALSE;
    }

    /* Create GIO channel for inotify events */
    app_data.laptop_backlight_io_channel = g_io_channel_unix_new(app_data.laptop_backlight_inotify_fd);
    g_io_channel_set_encoding(app_data.laptop_backlight_io_channel, NULL, NULL);
    g_io_channel_set_buffered(app_data.laptop_backlight_io_channel, FALSE);

    /* Setup watch for inotify events */
    app_data.laptop_backlight_watch_id = g_io_add_watch(app_data.laptop_backlight_io_channel,
                                                        G_IO_IN,
                                                        on_laptop_backlight_change,
                                                        NULL);

    /* Initialize last known brightness */
    app_data.last_laptop_brightness = laptop_backlight_read_brightness(app_data.laptop_backlight);

    g_message("Laptop backlight monitoring setup successfully (using inotify)");
    return TRUE;
}

/* Cleanup laptop backlight monitoring */
static void cleanup_laptop_backlight_monitoring(void)
{
    if (app_data.laptop_backlight_watch_id > 0) {
        g_source_remove(app_data.laptop_backlight_watch_id);
        app_data.laptop_backlight_watch_id = 0;
    }

    if (app_data.laptop_backlight_io_channel) {
        g_io_channel_unref(app_data.laptop_backlight_io_channel);
        app_data.laptop_backlight_io_channel = NULL;
    }

    if (app_data.laptop_backlight_watch_fd >= 0) {
        inotify_rm_watch(app_data.laptop_backlight_inotify_fd, app_data.laptop_backlight_watch_fd);
        app_data.laptop_backlight_watch_fd = -1;
    }

    if (app_data.laptop_backlight_inotify_fd >= 0) {
        close(app_data.laptop_backlight_inotify_fd);
        app_data.laptop_backlight_inotify_fd = -1;
    }
}

/* Handle laptop backlight change events */
static gboolean on_laptop_backlight_change(GIOChannel *channel, GIOCondition condition, gpointer data)
{
    (void)channel;
    (void)data;

    if (!(condition & G_IO_IN)) {
        return TRUE;
    }

    /* Read and discard inotify events */
    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len = read(app_data.laptop_backlight_inotify_fd, buffer, sizeof(buffer));

    if (len < 0 && errno != EAGAIN) {
        g_warning("Error reading inotify events: %s", strerror(errno));
        return TRUE;
    }

    /* Read the current laptop brightness */
    int current_brightness = laptop_backlight_read_brightness(app_data.laptop_backlight);
    if (current_brightness < 0) {
        return TRUE;
    }

    /* Check if brightness has actually changed */
    if (current_brightness == app_data.last_laptop_brightness) {
        return TRUE;  /* No change, ignore */
    }

    app_data.last_laptop_brightness = current_brightness;
    g_message("Laptop brightness changed to %d%%", current_brightness);

    /* Update all monitors that are in laptop display mode */
    if (app_data.monitors) {
        for (int i = 0; i < monitor_list_get_count(app_data.monitors); i++) {
            Monitor *monitor = monitor_list_get_monitor(app_data.monitors, i);
            AutoBrightnessMode mode = config_get_monitor_auto_brightness_mode(app_data.config,
                                                                              monitor_get_device_path(monitor));

            if (mode == AUTO_BRIGHTNESS_MODE_LAPTOP_DISPLAY) {
                /* Apply brightness offset */
                int offset = config_get_monitor_brightness_offset(app_data.config,
                                                                  monitor_get_device_path(monitor));
                int target_brightness = current_brightness + offset;

                /* Clamp to 0-100 range */
                if (target_brightness < 0) target_brightness = 0;
                if (target_brightness > 100) target_brightness = 100;

                /* Apply the new brightness */
                monitor_set_brightness_with_retry(monitor, target_brightness, auto_refresh_monitors_on_failure);

                /* Update UI if this is the current monitor */
                if (monitor == app_data.current_monitor) {
                    app_data.updating_from_auto = TRUE;
                    gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), target_brightness);
                    app_data.updating_from_auto = FALSE;
                    update_brightness_display();

                    g_message("Applied laptop brightness %d%% + offset %d%% -> %d%% to monitor",
                             current_brightness, offset, target_brightness);

#if HAVE_APPINDICATOR
                    /* Update tray icon label to reflect the new brightness */
                    update_tray_icon_label();
#endif
                }
            }
        }
    }

    return TRUE;  /* Keep the watch active */
}