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

/* Global application state */
typedef struct {
    GtkWidget *main_window;
    GtkWidget *monitor_combo;
    GtkWidget *brightness_scale;
    GtkWidget *brightness_label;
    GtkWidget *auto_brightness_check;
    GtkWidget *schedule_button;
    GtkWidget *start_minimized_check;
    GtkWidget *show_brightness_tray_check;
    
    MonitorList *monitors;
    Monitor *current_monitor;
    AppConfig *config;
    BrightnessScheduler *scheduler;
    
    gboolean updating_from_auto;
    guint auto_brightness_timer;
    gboolean start_minimized;
    
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
static void on_auto_brightness_toggled(GtkToggleButton *button, gpointer data);
static void on_schedule_clicked(GtkButton *button, gpointer data);
static void on_refresh_monitors_clicked(GtkButton *button, gpointer data);
static void on_start_minimized_toggled(GtkToggleButton *button, gpointer data);
static void on_show_brightness_tray_toggled(GtkToggleButton *button, gpointer data);
static gboolean auto_brightness_timer_callback(gpointer data);
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
static void on_indicator_auto_brightness_toggle(GtkMenuItem *item, gpointer data);
static void on_indicator_show_window(GtkMenuItem *item, gpointer data);
static void on_indicator_quit(GtkMenuItem *item, gpointer data);
static void update_indicator_menu(void);
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
    
    if (app_data.monitor_retry_timer > 0) {
        g_source_remove(app_data.monitor_retry_timer);
    }
    
    /* Cleanup udev monitoring */
#if HAVE_LIBUDEV
    cleanup_udev_monitoring();
#endif
    
    if (app_data.monitors) {
        monitor_list_free(app_data.monitors);
    }
    
    if (app_data.scheduler) {
        scheduler_free(app_data.scheduler);
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
    gint active = gtk_combo_box_get_active(combo);
    if (active >= 0 && app_data.monitors) {
        app_data.current_monitor = monitor_list_get_monitor(app_data.monitors, active);
        
        if (app_data.current_monitor) {
            /* Save as default monitor */
            config_set_default_monitor(app_data.config, 
                                     monitor_get_device_path(app_data.current_monitor));
            
            /* Read current brightness */
            int brightness = monitor_get_brightness_with_retry(app_data.current_monitor, auto_refresh_monitors_on_failure);
            if (brightness >= 0) {
                app_data.updating_from_auto = TRUE;
                gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), brightness);
                app_data.updating_from_auto = FALSE;
                update_brightness_display();
            }
            
            /* Load auto brightness setting for this monitor */
            gboolean auto_enabled = config_get_monitor_auto_brightness(app_data.config,
                                                                      monitor_get_device_path(app_data.current_monitor));
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), 
                                       auto_enabled);
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
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), FALSE);
        }
    }
}

/* Auto brightness checkbox toggled */
static void on_auto_brightness_toggled(GtkToggleButton *button, gpointer data)
{
    gboolean enabled = gtk_toggle_button_get_active(button);
    
    if (app_data.current_monitor) {
        /* Save setting per monitor */
        config_set_monitor_auto_brightness(app_data.config,
                                          monitor_get_device_path(app_data.current_monitor),
                                          enabled);
        
        /* Apply brightness immediately if auto brightness is being enabled */
        if (enabled) {
            /* Start timer if not already running (should already be running now) */
            if (app_data.auto_brightness_timer == 0) {
                app_data.auto_brightness_timer = g_timeout_add_seconds(60, 
                                                                      auto_brightness_timer_callback, 
                                                                      &app_data);
            }
            
            /* Apply current scheduled brightness immediately */
            int target_brightness = scheduler_get_current_brightness(app_data.scheduler);
            if (target_brightness >= 0) {
                monitor_set_brightness_with_retry(app_data.current_monitor, target_brightness, auto_refresh_monitors_on_failure);
                app_data.updating_from_auto = TRUE;
                gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), target_brightness);
                app_data.updating_from_auto = FALSE;
                update_brightness_display();
            }
        }
        /* Note: Timer now continues running to update menu even when auto brightness is disabled */
    }
    
    /* Update tray indicator menu */
#if HAVE_APPINDICATOR
    update_indicator_menu();
#endif
}

/* Schedule configuration button clicked */
static void on_schedule_clicked(GtkButton *button, gpointer data)
{
    /* This will open a schedule configuration dialog */
    /* Implementation in separate file for clarity */
    show_schedule_dialog(app_data.main_window, app_data.scheduler, app_data.config);
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

/* Auto brightness timer callback */
static gboolean auto_brightness_timer_callback(gpointer data)
{
    if (!app_data.current_monitor) {
        return TRUE; /* Continue timer */
    }
    
    /* Check if any monitor has auto brightness enabled */
    gboolean any_auto_enabled = FALSE;
    if (app_data.monitors) {
        for (int i = 0; i < monitor_list_get_count(app_data.monitors); i++) {
            Monitor *monitor = monitor_list_get_monitor(app_data.monitors, i);
            if (config_get_monitor_auto_brightness(app_data.config, 
                                                  monitor_get_device_path(monitor))) {
                any_auto_enabled = TRUE;
                
                /* Apply scheduled brightness to this monitor */
                int target_brightness = scheduler_get_current_brightness(app_data.scheduler);
                if (target_brightness >= 0) {
                    monitor_set_brightness_with_retry(monitor, target_brightness, auto_refresh_monitors_on_failure);
                    
                    /* Update UI if this is the current monitor */
                    if (monitor == app_data.current_monitor) {
                        app_data.updating_from_auto = TRUE;
                        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), target_brightness);
                        app_data.updating_from_auto = FALSE;
                        update_brightness_display();
                    }
                }
            }
        }
    }
    
    /* Update tray menu to show current scheduled brightness (always) */
#if HAVE_APPINDICATOR
    update_indicator_menu();
#endif
    
    /* Timer continues running regardless of auto brightness status to update menu */
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
    
    app_data.auto_brightness_check = gtk_check_button_new_with_label("Enable automatic brightness");
    gtk_box_pack_start(GTK_BOX(vbox), app_data.auto_brightness_check, FALSE, FALSE, 0);
    g_signal_connect(app_data.auto_brightness_check, "toggled", 
                     G_CALLBACK(on_auto_brightness_toggled), NULL);
    
    app_data.schedule_button = gtk_button_new_with_label("Configure Schedule");
    gtk_box_pack_start(GTK_BOX(vbox), app_data.schedule_button, FALSE, FALSE, 0);
    g_signal_connect(app_data.schedule_button, "clicked", 
                     G_CALLBACK(on_schedule_clicked), NULL);
    
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
                gtk_combo_box_set_active(GTK_COMBO_BOX(app_data.monitor_combo), i);
                break;
            }
        }
    }

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
    /* Update indicator menu to show/hide brightness */
    update_indicator_menu();
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
    
    /* Auto brightness toggle */
    GtkWidget *auto_brightness_item = gtk_check_menu_item_new_with_label("Auto Brightness");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(auto_brightness_item),
                                  gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check)));
    g_signal_connect(auto_brightness_item, "activate", G_CALLBACK(on_indicator_auto_brightness_toggle), NULL);
    
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
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), auto_brightness_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), separator1);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), show_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), separator2);
    gtk_menu_shell_append(GTK_MENU_SHELL(app_data.indicator_menu), quit_item);
    
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
        monitor_set_brightness_with_retry(app_data.current_monitor, 20, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 20);
        app_data.updating_from_auto = FALSE;
        update_brightness_display();
        
        /* Disable auto brightness */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), FALSE);
        }
    }
}

static void on_indicator_brightness_25(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        monitor_set_brightness_with_retry(app_data.current_monitor, 25, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 25);
        app_data.updating_from_auto = FALSE;
        update_brightness_display();
        
        /* Disable auto brightness */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), FALSE);
        }
    }
}

static void on_indicator_brightness_50(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        monitor_set_brightness_with_retry(app_data.current_monitor, 50, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 50);
        app_data.updating_from_auto = FALSE;
        update_brightness_display();
        
        /* Disable auto brightness */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), FALSE);
        }
    }
}

static void on_indicator_brightness_35(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        monitor_set_brightness_with_retry(app_data.current_monitor, 35, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 35);
        app_data.updating_from_auto = FALSE;
        update_brightness_display();
        
        /* Disable auto brightness */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), FALSE);
        }
    }
}

static void on_indicator_brightness_70(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        monitor_set_brightness_with_retry(app_data.current_monitor, 70, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 70);
        app_data.updating_from_auto = FALSE;
        update_brightness_display();
        
        /* Disable auto brightness */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), FALSE);
        }
    }
}

static void on_indicator_brightness_100(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (app_data.current_monitor) {
        monitor_set_brightness_with_retry(app_data.current_monitor, 100, auto_refresh_monitors_on_failure);
        app_data.updating_from_auto = TRUE;
        gtk_range_set_value(GTK_RANGE(app_data.brightness_scale), 100);
        app_data.updating_from_auto = FALSE;
        update_brightness_display();
        
        /* Disable auto brightness */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check))) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), FALSE);
        }
    }
}

static void on_indicator_auto_brightness_toggle(GtkMenuItem *item, gpointer data)
{
    (void)data;
    gboolean active = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check), active);
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
    
    if (show_brightness && app_data.current_monitor) {
        /* Show brightness percentage next to icon */
        int brightness = (int)gtk_range_get_value(GTK_RANGE(app_data.brightness_scale));
        char label[16];
        snprintf(label, sizeof(label), "%d%%", brightness);
        app_indicator_set_label(app_data.indicator, label, "100%");
    } else {
        /* Clear label */
        app_indicator_set_label(app_data.indicator, "", "");
    }
}

static void update_indicator_menu(void)
{
    gboolean show_brightness_in_tray = config_get_show_brightness_in_tray(app_data.config);
    
    /* Update menu items */
    GList *children = gtk_container_get_children(GTK_CONTAINER(app_data.indicator_menu));
    for (GList *iter = children; iter; iter = iter->next) {
        GtkWidget *child = GTK_WIDGET(iter->data);
        
        if (GTK_IS_CHECK_MENU_ITEM(child)) {
            const char *label = gtk_menu_item_get_label(GTK_MENU_ITEM(child));
            if (label && (strcmp(label, "Auto Brightness") == 0 || strncmp(label, "Auto Brightness:", 16) == 0)) {
                gboolean main_active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app_data.auto_brightness_check));
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(child), main_active);
                
                /* Update label to show scheduled brightness value */
                int scheduled_brightness = scheduler_get_current_brightness(app_data.scheduler);
                if (scheduled_brightness >= 0) {
                    char new_label[32];
                    snprintf(new_label, sizeof(new_label), "Auto Brightness: %d%%", scheduled_brightness);
                    gtk_menu_item_set_label(GTK_MENU_ITEM(child), new_label);
                } else {
                    gtk_menu_item_set_label(GTK_MENU_ITEM(child), "Auto Brightness");
                }
            }
        } else if (GTK_IS_MENU_ITEM(child)) {
            const char *label = gtk_menu_item_get_label(GTK_MENU_ITEM(child));
            if (label && strncmp(label, "Brightness", 10) == 0) {
                /* Update brightness menu item label */
                if (!show_brightness_in_tray && app_data.current_monitor) {
                    int brightness = (int)gtk_range_get_value(GTK_RANGE(app_data.brightness_scale));
                    char new_label[32];
                    snprintf(new_label, sizeof(new_label), "Brightness: %d%%", brightness);
                    gtk_menu_item_set_label(GTK_MENU_ITEM(child), new_label);
                } else {
                    gtk_menu_item_set_label(GTK_MENU_ITEM(child), "Brightness");
                }
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