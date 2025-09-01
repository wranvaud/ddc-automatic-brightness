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
    
    return TRUE;
}

/* Check if monitor is available */
gboolean monitor_is_available(Monitor *monitor)
{
    return monitor ? monitor->available : FALSE;
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