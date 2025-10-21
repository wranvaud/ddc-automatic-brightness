/*
 * monitor_detect.c - Monitor detection implementation
 */

#include "monitor_detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>

/* Check if an i2c device corresponds to an internal display (eDP or LVDS) */
static gboolean is_internal_display(const char *device_path)
{
    if (!device_path) {
        return FALSE;
    }

    /* Extract i2c bus number from device path (e.g., "/dev/i2c-12" -> "12") */
    const char *dash = strrchr(device_path, '-');
    if (!dash) {
        return FALSE;
    }

    int i2c_num = atoi(dash + 1);

    /* Look for DRM connector associated with this i2c device */
    DIR *drm_dir = opendir("/sys/class/drm");
    if (!drm_dir) {
        return FALSE;
    }

    gboolean is_internal = FALSE;
    struct dirent *entry;
    char path[PATH_MAX];
    char link_target[PATH_MAX];

    while ((entry = readdir(drm_dir)) != NULL) {
        /* Look for card*-eDP-* or card*-LVDS-* entries */
        if (strstr(entry->d_name, "card") &&
            (strstr(entry->d_name, "-eDP-") || strstr(entry->d_name, "-LVDS-"))) {

            /* Check if this connector has a DDC/i2c device matching our number */
            snprintf(path, sizeof(path), "/sys/class/drm/%s/ddc", entry->d_name);

            ssize_t len = readlink(path, link_target, sizeof(link_target) - 1);
            if (len > 0) {
                link_target[len] = '\0';

                /* Check if the symlink contains our i2c number */
                char i2c_pattern[32];
                snprintf(i2c_pattern, sizeof(i2c_pattern), "i2c-%d", i2c_num);

                if (strstr(link_target, i2c_pattern)) {
                    is_internal = TRUE;
                    g_message("Detected internal display: %s via DRM connector %s",
                             device_path, entry->d_name);
                    break;
                }
            }
        }
    }

    closedir(drm_dir);
    return is_internal;
}

/* Comparator function for sorting monitors - external monitors first */
static gint monitor_compare_func(gconstpointer a, gconstpointer b)
{
    const Monitor *mon_a = (const Monitor *)a;
    const Monitor *mon_b = (const Monitor *)b;

    gboolean is_internal_a = monitor_is_internal((Monitor *)mon_a);
    gboolean is_internal_b = monitor_is_internal((Monitor *)mon_b);

    /* External monitors (FALSE) should come before internal monitors (TRUE) */
    if (is_internal_a != is_internal_b) {
        return is_internal_a ? 1 : -1;
    }

    /* If both are the same type, maintain original order (stable sort) */
    return 0;
}

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
                /* Check if this is an internal display */
                gboolean is_internal = is_internal_display(current_device);

                /* Create display name with Internal/External label */
                char display_name[256];
                const char *type_label = is_internal ? "Internal" : "External";

                if (strlen(current_name) > 0) {
                    snprintf(display_name, sizeof(display_name), "%s (%s - %s)",
                            current_name, type_label, current_device);
                } else {
                    snprintf(display_name, sizeof(display_name), "Monitor (%s - %s)",
                            type_label, current_device);
                }

                Monitor *monitor = monitor_new(current_device, display_name);
                monitor_set_internal(monitor, is_internal);

                monitor_list_add(list, monitor);
                g_message("Found monitor: %s (%s)", current_device, type_label);
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
        /* Check if this is an internal display */
        gboolean is_internal = is_internal_display(current_device);

        /* Create display name with Internal/External label */
        char display_name[256];
        const char *type_label = is_internal ? "Internal" : "External";

        if (strlen(current_name) > 0) {
            snprintf(display_name, sizeof(display_name), "%s (%s - %s)",
                    current_name, type_label, current_device);
        } else {
            snprintf(display_name, sizeof(display_name), "Monitor (%s - %s)",
                    type_label, current_device);
        }

        Monitor *monitor = monitor_new(current_device, display_name);
        monitor_set_internal(monitor, is_internal);

        monitor_list_add(list, monitor);
        g_message("Found monitor: %s (%s)", current_device, type_label);
    }
    
    regfree(&device_regex);
    regfree(&name_regex);
    pclose(fp);

    if (monitor_list_get_count(list) == 0) {
        g_warning("No DDC/CI compatible monitors found");
    } else {
        /* Sort monitors: external monitors first, then internal */
        monitor_list_sort(list, monitor_compare_func);
        g_message("Sorted %d monitor(s) - external monitors prioritized",
                 monitor_list_get_count(list));
    }

    return list;
}

/* Test if ddccontrol is available */
gboolean monitor_detect_ddccontrol_available(void)
{
    int result = system("which ddccontrol >/dev/null 2>&1");
    return (result == 0);
}