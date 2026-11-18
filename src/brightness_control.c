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
#include <limits.h>
#include <time.h>

#define DDC_FAILURE_THRESHOLD 3   /* consecutive DDC failures before per-monitor backoff */
#define DDC_BACKOFF_SECONDS   45  /* how long to pause DDC per monitor after threshold hit */

/* Check DRM connector link state without touching the AUX bus.
 * Returns TRUE (assume connected) if connector path is unknown or unreadable. */
static gboolean drm_connector_is_connected(const char *connector)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/class/drm/%s/status", connector);

    FILE *f = fopen(path, "r");
    if (!f) return TRUE;

    char status[32] = {0};
    (void)fgets(status, sizeof(status), f);
    fclose(f);

    char *nl = strchr(status, '\n');
    if (nl) *nl = '\0';

    return (strcmp(status, "connected") == 0);
}

/* Monitor structure */
struct _Monitor {
    char *device_path;
    char *display_name;
    char *model_name;         /* Raw model name from ddccontrol (e.g. "Samsung standard LCD") */
    char *drm_connector;      /* DRM sysfs name (e.g. "card1-DP-1"), NULL if unknown */
    char *config_key;         /* Stable EDID-based settings key, NULL if unknown */
    gboolean available;
    gboolean is_internal;
    int current_brightness;  /* Last brightness value actually sent to monitor (-1 = unknown) */
    int target_brightness;   /* Target brightness for gradual transitions (-1 = no transition) */
    double stable_lux;       /* Last lux value used to set brightness (for hysteresis, -1.0 = unknown) */
    int ddc_failure_count;   /* Consecutive DDC call failures (resets on success) */
    time_t ddc_backoff_until; /* Skip DDC calls for this monitor until this time (0 = no backoff) */
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
    monitor->model_name = NULL;
    monitor->drm_connector = NULL;
    monitor->config_key = NULL;
    monitor->available = TRUE;
    monitor->is_internal = FALSE;  /* Will be set during detection */
    monitor->current_brightness = -1;  /* Unknown initial brightness */
    monitor->target_brightness = -1;   /* No transition pending */
    monitor->stable_lux = -1.0;        /* Unknown initial lux */
    monitor->ddc_failure_count = 0;
    monitor->ddc_backoff_until = 0;

    return monitor;
}

/* Free monitor */
void monitor_free(Monitor *monitor)
{
    if (monitor) {
        g_free(monitor->device_path);
        g_free(monitor->display_name);
        g_free(monitor->model_name);
        g_free(monitor->drm_connector);
        g_free(monitor->config_key);
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

/* Get raw model name (e.g. "Samsung standard LCD") */
const char* monitor_get_model_name(Monitor *monitor)
{
    return monitor ? monitor->model_name : NULL;
}

/* Set raw model name */
void monitor_set_model_name(Monitor *monitor, const char *model_name)
{
    if (!monitor) return;
    g_free(monitor->model_name);
    monitor->model_name = model_name ? g_strdup(model_name) : NULL;
}

/* Get DRM connector sysfs name */
const char* monitor_get_drm_connector(Monitor *monitor)
{
    return monitor ? monitor->drm_connector : NULL;
}

/* Set DRM connector sysfs name */
void monitor_set_drm_connector(Monitor *monitor, const char *connector)
{
    if (!monitor) return;
    g_free(monitor->drm_connector);
    monitor->drm_connector = connector ? g_strdup(connector) : NULL;
}

/* Get the settings key: EDID identity when known, otherwise the device path */
const char* monitor_get_config_key(Monitor *monitor)
{
    if (!monitor) return NULL;
    return monitor->config_key ? monitor->config_key : monitor->device_path;
}

/* Set the EDID-based settings key */
void monitor_set_config_key(Monitor *monitor, const char *key)
{
    if (!monitor) return;
    g_free(monitor->config_key);
    monitor->config_key = key ? g_strdup(key) : NULL;
}

/* Check if this monitor is currently backing off DDC calls after repeated failures */
gboolean monitor_is_ddc_backoff_active(Monitor *monitor)
{
    return monitor && time(NULL) < monitor->ddc_backoff_until;
}

/* Check if monitor is internal display */
gboolean monitor_is_internal(const Monitor *monitor)
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

    /* Fast non-blocking pre-check: skip DDC entirely if DP link is known down */
    if (monitor->drm_connector &&
        !drm_connector_is_connected(monitor->drm_connector)) {
        g_message("Skipping DDC read for %s — connector %s is disconnected",
                  monitor->device_path, monitor->drm_connector);
        monitor->available = FALSE;
        return -1;
    }

    /* Fast pre-check: skip DDC entirely while this monitor is backing off
     * after repeated failures, so a degrading DDC/AUX channel doesn't keep
     * getting hammered while it's trying to recover. */
    if (monitor_is_ddc_backoff_active(monitor)) {
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

    /* Compile regex once before the loop for better performance */
    regex_t regex;
    regmatch_t matches[3];
    int regex_compiled = (regcomp(&regex, "Control 0x10: \\+/([0-9]+)/([0-9]+)", REG_EXTENDED) == 0);

    if (!regex_compiled) {
        g_warning("Failed to compile regex for brightness parsing");
        pclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        /* Parse output: "Control 0x10: +/current/max [...]" */
        if (regexec(&regex, line, 3, matches, 0) == 0) {
            /* Extract current value */
            char current_str[16];
            int len = matches[1].rm_eo - matches[1].rm_so;
            if (len < sizeof(current_str)) {
                strncpy(current_str, line + matches[1].rm_so, len);
                current_str[len] = '\0';
                brightness = atoi(current_str);
                break;  /* Found match, exit loop */
            }
        }
    }

    regfree(&regex);
    pclose(fp);
    
    if (brightness < 0) {
        monitor->ddc_failure_count++;
        if (monitor->ddc_failure_count >= DDC_FAILURE_THRESHOLD) {
            monitor->ddc_backoff_until = time(NULL) + DDC_BACKOFF_SECONDS;
            g_warning("Monitor %s: %d consecutive DDC failures — backing off for %ds",
                      monitor->device_path, monitor->ddc_failure_count, DDC_BACKOFF_SECONDS);
        } else {
            g_warning("Failed to read brightness from monitor %s (failure %d/%d)",
                      monitor->device_path, monitor->ddc_failure_count, DDC_FAILURE_THRESHOLD);
        }
        monitor->available = FALSE;
    } else {
        monitor->ddc_failure_count = 0;
        monitor->ddc_backoff_until = 0;
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

    /* Fast non-blocking pre-check: skip DDC entirely if DP link is known down */
    if (monitor->drm_connector &&
        !drm_connector_is_connected(monitor->drm_connector)) {
        g_message("Skipping DDC write for %s — connector %s is disconnected",
                  monitor->device_path, monitor->drm_connector);
        monitor->available = FALSE;
        return FALSE;
    }

    /* Fast pre-check: skip DDC entirely while this monitor is backing off
     * after repeated failures, so a degrading DDC/AUX channel doesn't keep
     * getting hammered while it's trying to recover. */
    if (monitor_is_ddc_backoff_active(monitor)) {
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
        monitor->ddc_failure_count++;
        if (monitor->ddc_failure_count >= DDC_FAILURE_THRESHOLD) {
            monitor->ddc_backoff_until = time(NULL) + DDC_BACKOFF_SECONDS;
            g_warning("Monitor %s: %d consecutive DDC failures — backing off for %ds",
                      monitor->device_path, monitor->ddc_failure_count, DDC_BACKOFF_SECONDS);
        } else {
            g_warning("Failed to set brightness on monitor %s (failure %d/%d)",
                      monitor->device_path, monitor->ddc_failure_count, DDC_FAILURE_THRESHOLD);
        }
        monitor->available = FALSE;
        return FALSE;
    }

    /* Success: reset failure counter/backoff and update tracked brightness */
    monitor->ddc_failure_count = 0;
    monitor->ddc_backoff_until = 0;
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

    /* If first attempt failed and we have a refresh callback, trigger auto-refresh
     * The refresh_callback itself now has rate limiting to prevent runaway loops */
    if (!success && refresh_callback && monitor->available == FALSE) {
        /* Only trigger refresh if this is a new failure, not a redundant retry attempt
         * Skip if brightness value hasn't changed - this prevents feedback loops from
         * repeated set attempts with the same value */
        if (monitor->current_brightness != brightness) {
            g_message("Brightness set failed (monitor unavailable), attempting auto-refresh...");

            /* Trigger refresh - this may recreate monitor objects.
             * We don't retry here because the monitor pointer may be stale after refresh.
             * The caller should retry with the updated monitor reference. */
            if (refresh_callback()) {
                g_message("Auto-refresh completed - caller should retry with updated monitor");
            } else {
                g_warning("Auto-refresh failed");
            }
        } else {
            g_debug("Skipping auto-refresh for redundant brightness set attempt");
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