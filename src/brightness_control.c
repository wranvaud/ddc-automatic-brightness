/*
 * brightness_control.c - Monitor brightness control implementation
 */

#include "brightness_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <regex.h>

/* Monitor structure */
struct _Monitor {
    char *device_path;
    char *display_name;
    gboolean available;
    gboolean is_internal;
    int current_brightness;  /* Last brightness value actually sent to monitor (-1 = unknown) */
    int target_brightness;   /* Target brightness for gradual transitions (-1 = no transition) */
    double stable_lux;       /* Last lux value used to set brightness (for hysteresis, -1.0 = unknown) */
};

/* Monitor list structure */
struct _MonitorList {
    GList *monitors;
};

/* Create new monitor */
Monitor* monitor_new(const char *device_path, const char *name)
{
    Monitor *monitor = g_new0(Monitor, 1);
    monitor->device_path = g_strdup(device_path);
    monitor->display_name = g_strdup(name ? name : device_path);
    monitor->available = TRUE;
    monitor->is_internal = FALSE;  /* Will be set during detection */
    monitor->current_brightness = -1;  /* Unknown initial brightness */
    monitor->target_brightness = -1;   /* No transition pending */
    monitor->stable_lux = -1.0;        /* Unknown initial lux */

    return monitor;
}

/* Free monitor */
void monitor_free(Monitor *monitor)
{
    if (monitor) {
        g_free(monitor->device_path);
        g_free(monitor->display_name);
        g_free(monitor);
    }
}

/* Get monitor device path */
const char* monitor_get_device_path(Monitor *monitor)
{
    return monitor ? monitor->device_path : NULL;
}

/* Get monitor display name */
const char* monitor_get_display_name(Monitor *monitor)
{
    return monitor ? monitor->display_name : NULL;
}

/* Check if monitor is internal display */
gboolean monitor_is_internal(Monitor *monitor)
{
    return monitor ? monitor->is_internal : FALSE;
}

/* Set monitor internal display flag */
void monitor_set_internal(Monitor *monitor, gboolean is_internal)
{
    if (monitor) {
        monitor->is_internal = is_internal;
    }
}

/* Get current brightness from monitor */
int monitor_get_brightness(Monitor *monitor)
{
    if (!monitor || !monitor->available) {
        return -1;
    }
    
    /* Execute ddccontrol command to read brightness */
    char command[256];
    snprintf(command, sizeof(command), "ddccontrol -r 0x10 dev:%s 2>/dev/null", 
             monitor->device_path);
    
    FILE *fp = popen(command, "r");
    if (!fp) {
        g_warning("Failed to execute ddccontrol command");
        return -1;
    }
    
    char line[512];
    int brightness = -1;
    
    while (fgets(line, sizeof(line), fp)) {
        /* Parse output: "Control 0x10: +/current/max [...]" */
        regex_t regex;
        regmatch_t matches[3];
        
        if (regcomp(&regex, "Control 0x10: \\+/([0-9]+)/([0-9]+)", REG_EXTENDED) == 0) {
            if (regexec(&regex, line, 3, matches, 0) == 0) {
                /* Extract current value */
                char current_str[16];
                int len = matches[1].rm_eo - matches[1].rm_so;
                if (len < sizeof(current_str)) {
                    strncpy(current_str, line + matches[1].rm_so, len);
                    current_str[len] = '\0';
                    brightness = atoi(current_str);
                }
            }
            regfree(&regex);
        }
        
        if (brightness >= 0) {
            break;
        }
    }
    
    pclose(fp);
    
    if (brightness < 0) {
        g_warning("Failed to read brightness from monitor %s", monitor->device_path);
        monitor->available = FALSE;
    }
    
    return brightness;
}

/* Set monitor brightness */
gboolean monitor_set_brightness(Monitor *monitor, int brightness)
{
    if (!monitor || !monitor->available) {
        return FALSE;
    }

    if (brightness < 0 || brightness > 100) {
        g_warning("Invalid brightness value: %d", brightness);
        return FALSE;
    }

    /* Skip redundant commands - don't send if brightness unchanged */
    if (monitor->current_brightness == brightness) {
        g_debug("Brightness unchanged at %d%% for %s, skipping DDC-CI command",
                brightness, monitor->device_path);
        return TRUE;  /* Not an error, just unnecessary */
    }

    /* Execute ddccontrol command to set brightness */
    char command[256];
    snprintf(command, sizeof(command), "ddccontrol -r 0x10 -w %d dev:%s >/dev/null 2>&1",
             brightness, monitor->device_path);

    int result = system(command);

    if (result != 0) {
        g_warning("Failed to set brightness on monitor %s", monitor->device_path);
        monitor->available = FALSE;
        return FALSE;
    }

    /* Update current brightness tracking on success */
    monitor->current_brightness = brightness;
    g_debug("Successfully set brightness to %d%% for %s", brightness, monitor->device_path);

    return TRUE;
}

/* Check if monitor is available */
gboolean monitor_is_available(Monitor *monitor)
{
    return monitor ? monitor->available : FALSE;
}

/* Set monitor availability status */
void monitor_set_available(Monitor *monitor, gboolean available)
{
    if (monitor) {
        monitor->available = available;
    }
}

/* Get current brightness (last value actually sent to monitor) */
int monitor_get_current_brightness(Monitor *monitor)
{
    return monitor ? monitor->current_brightness : -1;
}

/* Get target brightness (for gradual transitions) */
int monitor_get_target_brightness(Monitor *monitor)
{
    return monitor ? monitor->target_brightness : -1;
}

/* Set target brightness (for gradual transitions) */
void monitor_set_target_brightness(Monitor *monitor, int brightness)
{
    if (monitor) {
        monitor->target_brightness = brightness;
    }
}

/* Get stable lux value (last lux used to set brightness) */
double monitor_get_stable_lux(Monitor *monitor)
{
    return monitor ? monitor->stable_lux : -1.0;
}

/* Set stable lux value (for hysteresis) */
void monitor_set_stable_lux(Monitor *monitor, double lux)
{
    if (monitor) {
        monitor->stable_lux = lux;
    }
}

/* Get brightness with auto-refresh retry capability */
int monitor_get_brightness_with_retry(Monitor *monitor, MonitorRefreshCallback refresh_callback)
{
    if (!monitor) {
        return -1;
    }

    /* First attempt */
    int brightness = monitor_get_brightness(monitor);

    /* If first attempt failed and we have a refresh callback, trigger auto-refresh */
    if (brightness < 0 && refresh_callback && monitor->available == FALSE) {
        g_message("Brightness read failed, attempting auto-refresh...");

        /* Trigger refresh - this may recreate monitor objects.
         * We don't retry here because the monitor pointer may be stale after refresh.
         * The caller should retry with the updated monitor reference. */
        if (refresh_callback()) {
            g_message("Auto-refresh completed - caller should retry with updated monitor");
        } else {
            g_warning("Auto-refresh failed");
        }
    }

    return brightness;
}

/* Set brightness with auto-refresh retry capability */
gboolean monitor_set_brightness_with_retry(Monitor *monitor, int brightness, MonitorRefreshCallback refresh_callback)
{
    if (!monitor) {
        return FALSE;
    }

    /* First attempt */
    gboolean success = monitor_set_brightness(monitor, brightness);

    /* If first attempt failed and we have a refresh callback, trigger auto-refresh */
    if (!success && refresh_callback && monitor->available == FALSE) {
        g_message("Brightness set failed, attempting auto-refresh...");

        /* Trigger refresh - this may recreate monitor objects.
         * We don't retry here because the monitor pointer may be stale after refresh.
         * The caller should retry with the updated monitor reference. */
        if (refresh_callback()) {
            g_message("Auto-refresh completed - caller should retry with updated monitor");
        } else {
            g_warning("Auto-refresh failed");
        }
    }

    return success;
}

/* Create new monitor list */
MonitorList* monitor_list_new(void)
{
    MonitorList *list = g_new0(MonitorList, 1);
    list->monitors = NULL;
    return list;
}

/* Free monitor list */
void monitor_list_free(MonitorList *list)
{
    if (list) {
        g_list_free_full(list->monitors, (GDestroyNotify)monitor_free);
        g_free(list);
    }
}

/* Add monitor to list */
void monitor_list_add(MonitorList *list, Monitor *monitor)
{
    if (list && monitor) {
        list->monitors = g_list_append(list->monitors, monitor);
    }
}

/* Get monitor by index */
Monitor* monitor_list_get_monitor(MonitorList *list, int index)
{
    if (!list || index < 0) {
        return NULL;
    }
    
    GList *item = g_list_nth(list->monitors, index);
    return item ? (Monitor*)item->data : NULL;
}

/* Get monitor count */
int monitor_list_get_count(MonitorList *list)
{
    return list ? g_list_length(list->monitors) : 0;
}

/* Sort monitor list using provided comparison function */
void monitor_list_sort(MonitorList *list, GCompareFunc compare_func)
{
    if (list && compare_func) {
        list->monitors = g_list_sort(list->monitors, compare_func);
    }
}