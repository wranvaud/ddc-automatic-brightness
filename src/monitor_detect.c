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
    regex_t device_regex, name_regex;
    regmatch_t matches[3];
    
    /* Compile regex to match device lines and monitor name lines */
    if (regcomp(&device_regex, "Device: dev:(/dev/i2c-[0-9]+)", REG_EXTENDED) != 0) {
        g_warning("Failed to compile device regex");
        pclose(fp);
        return list;
    }
    
    if (regcomp(&name_regex, "Monitor Name: (.+)", REG_EXTENDED) != 0) {
        g_warning("Failed to compile name regex");
        regfree(&device_regex);
        pclose(fp);
        return list;
    }
    
    char current_device[64] = "";
    char current_name[128] = "";
    gboolean ddc_supported = FALSE;
    
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        /* Look for device path */
        if (regexec(&device_regex, line, 3, matches, 0) == 0) {
            /* If we have a previous monitor with DDC support, add it */
            if (strlen(current_device) > 0 && ddc_supported) {
                char display_name[256];
                if (strlen(current_name) > 0) {
                    snprintf(display_name, sizeof(display_name), "%s (%s)", current_name, current_device);
                } else {
                    snprintf(display_name, sizeof(display_name), "Monitor (%s)", current_device);
                }
                
                Monitor *monitor = monitor_new(current_device, display_name);
                monitor_list_add(list, monitor);
                g_message("Found monitor: %s", current_device);
            }
            
            /* Extract new device path */
            int len = matches[1].rm_eo - matches[1].rm_so;
            if (len < sizeof(current_device)) {
                strncpy(current_device, line + matches[1].rm_so, len);
                current_device[len] = '\0';
                current_name[0] = '\0';  /* Reset name */
                ddc_supported = FALSE;   /* Reset DDC support flag */
            }
        }
        
        /* Look for DDC/CI support */
        if (strstr(line, "DDC/CI supported: Yes")) {
            ddc_supported = TRUE;
        }
        
        /* Look for monitor name */
        if (regexec(&name_regex, line, 3, matches, 0) == 0) {
            int len = matches[1].rm_eo - matches[1].rm_so;
            if (len < sizeof(current_name)) {
                strncpy(current_name, line + matches[1].rm_so, len);
                current_name[len] = '\0';
            }
        }
    }
    
    /* Add the last monitor if it has DDC support */
    if (strlen(current_device) > 0 && ddc_supported) {
        char display_name[256];
        if (strlen(current_name) > 0) {
            snprintf(display_name, sizeof(display_name), "%s (%s)", current_name, current_device);
        } else {
            snprintf(display_name, sizeof(display_name), "Monitor (%s)", current_device);
        }
        
        Monitor *monitor = monitor_new(current_device, display_name);
        monitor_list_add(list, monitor);
        g_message("Found monitor: %s", current_device);
    }
    
    regfree(&device_regex);
    regfree(&name_regex);
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