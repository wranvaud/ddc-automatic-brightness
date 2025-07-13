/*
 * monitor_detect.c - Monitor detection implementation
 */

#include "monitor_detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

/* Detect all available monitors */
MonitorList* monitor_detect_all(void)
{
    MonitorList *list = monitor_list_new();
    
    /* Check if ddccontrol is available */
    if (!monitor_detect_ddccontrol_available()) {
        g_warning("ddccontrol command not found");
        return list;
    }
    
    /* Execute ddccontrol -p to probe for monitors */
    FILE *fp = popen("ddccontrol -p 2>/dev/null", "r");
    if (!fp) {
        g_warning("Failed to execute ddccontrol -p");
        return list;
    }
    
    char line[512];
    regex_t regex;
    regmatch_t matches[3];
    
    /* Compile regex to match device lines */
    if (regcomp(&regex, "dev:(/dev/i2c-[0-9]+)", REG_EXTENDED) != 0) {
        g_warning("Failed to compile regex");
        pclose(fp);
        return list;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        /* Look for lines containing device paths */
        if (regexec(&regex, line, 3, matches, 0) == 0) {
            /* Extract device path */
            char device_path[64];
            int len = matches[1].rm_eo - matches[1].rm_so;
            if (len < sizeof(device_path)) {
                strncpy(device_path, line + matches[1].rm_so, len);
                device_path[len] = '\0';
                
                /* Create display name from device path */
                char display_name[128];
                snprintf(display_name, sizeof(display_name), "Monitor (%s)", device_path);
                
                /* Create monitor and add to list */
                Monitor *monitor = monitor_new(device_path, display_name);
                monitor_list_add(list, monitor);
                
                g_message("Found monitor: %s", device_path);
            }
        }
    }
    
    regfree(&regex);
    pclose(fp);
    
    if (monitor_list_get_count(list) == 0) {
        g_warning("No DDC/CI compatible monitors found");
    }
    
    return list;
}

/* Test if ddccontrol is available */
gboolean monitor_detect_ddccontrol_available(void)
{
    int result = system("which ddccontrol >/dev/null 2>&1");
    return (result == 0);
}