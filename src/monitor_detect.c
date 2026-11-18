/*
 * monitor_detect.c - Monitor detection implementation
 */

#define _POSIX_C_SOURCE 200809L  /* readlink() under -std=c99 */

#include "monitor_detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>

/* Find the DRM connector sysfs name for a given i2c bus number.
 * Scans /sys/class/drm/<connector>/ddc symlinks.
 * Caller must g_free() the result.  Returns NULL if no match found. */
static char* find_drm_connector_for_i2c(int i2c_num)
{
    DIR *drm_dir = opendir("/sys/class/drm");
    if (!drm_dir) return NULL;

    char *connector_name = NULL;
    struct dirent *entry;
    char path[PATH_MAX];
    char link_target[PATH_MAX];
    char i2c_pattern[32];

    snprintf(i2c_pattern, sizeof(i2c_pattern), "i2c-%d", i2c_num);

    while ((entry = readdir(drm_dir)) != NULL) {
        if (!strstr(entry->d_name, "card")) continue;

        snprintf(path, sizeof(path), "/sys/class/drm/%s/ddc", entry->d_name);
        ssize_t len = readlink(path, link_target, sizeof(link_target) - 1);
        if (len <= 0) continue;
        link_target[len] = '\0';

        /* Compare the trailing path component exactly (e.g. "i2c-14"), not a
         * substring, so bus numbers that are prefixes of one another (i2c-1
         * vs i2c-10, i2c-11, ...) can't match the wrong connector. */
        const char *base = strrchr(link_target, '/');
        base = base ? base + 1 : link_target;

        if (strcmp(base, i2c_pattern) == 0) {
            connector_name = g_strdup(entry->d_name);
            break;
        }
    }

    closedir(drm_dir);
    return connector_name;
}

/* Extract the bus number from a device path such as "/dev/i2c-12". Returns -1 if none. */
static int i2c_bus_number(const char *device_path)
{
    const char *dash = device_path ? strrchr(device_path, '-') : NULL;
    if (!dash || !g_ascii_isdigit(dash[1])) return -1;
    return atoi(dash + 1);
}

/* An internal display is one whose DRM connector is eDP or LVDS. Uses the exact
 * bus match in find_drm_connector_for_i2c(), so i2c-1 can't match i2c-12. */
static gboolean is_internal_connector(const char *connector)
{
    return connector && (strstr(connector, "-eDP-") || strstr(connector, "-LVDS-"));
}

/* Keep only characters that are safe in a GKeyFile key and a group name */
static void sanitize_key_part(char *s)
{
    for (; *s; s++) {
        if (!g_ascii_isalnum(*s) && *s != '.' && *s != '_') *s = '_';
    }
}

/* Read the EDID of a DRM connector and build a stable identity for the monitor.
 * key:  "edid-<maker>-<model name or product code>-<serial>" (always set on success)
 * name: the monitor name from the EDID, e.g. "U32J59x" (NULL if the EDID has none)
 * Returns FALSE if no EDID could be read. Caller frees both strings. */
static gboolean read_edid_identity(const char *connector, char **key, char **name)
{
    *key = NULL;
    *name = NULL;
    if (!connector) return FALSE;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/class/drm/%s/edid", connector);

    guchar edid[128];
    FILE *f = fopen(path, "rb");
    if (!f) return FALSE;
    size_t n = fread(edid, 1, sizeof(edid), f);
    fclose(f);

    static const guchar header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    if (n < sizeof(edid) || memcmp(edid, header, sizeof(header)) != 0) return FALSE;

    /* Manufacturer: three 5-bit letters packed big-endian in bytes 8-9 */
    guint16 m = (guint16)((edid[8] << 8) | edid[9]);
    char maker[4] = {
        (char)('@' + ((m >> 10) & 0x1F)),
        (char)('@' + ((m >> 5) & 0x1F)),
        (char)('@' + (m & 0x1F)),
        '\0'
    };
    guint16 product = (guint16)(edid[10] | (edid[11] << 8));
    guint32 serial_num = (guint32)edid[12] | ((guint32)edid[13] << 8) |
                         ((guint32)edid[14] << 16) | ((guint32)edid[15] << 24);

    /* Descriptor blocks: 0xFC = monitor name, 0xFF = serial number string */
    char model[14] = "", serial[14] = "";
    for (int off = 54; off <= 108; off += 18) {
        const guchar *d = edid + off;
        if (d[0] != 0 || d[1] != 0 || (d[3] != 0xFC && d[3] != 0xFF)) continue;
        char *dst = (d[3] == 0xFC) ? model : serial;
        memcpy(dst, d + 5, 13);
        dst[13] = '\0';
        char *nl = strchr(dst, '\n');
        if (nl) *nl = '\0';
        g_strstrip(dst);
    }

    char *model_part = model[0] ? g_strdup(model) : g_strdup_printf("%04X", product);
    char *serial_part = serial[0] ? g_strdup(serial) : g_strdup_printf("%u", serial_num);
    sanitize_key_part(model_part);
    sanitize_key_part(serial_part);
    sanitize_key_part(maker);

    *key = g_strdup_printf("edid-%s-%s-%s", maker, model_part, serial_part);
    if (model[0]) *name = g_strdup(model);

    g_free(model_part);
    g_free(serial_part);
    return TRUE;
}

/* Build a Monitor for a ddccontrol-detected device and add it to the list */
static void add_detected_monitor(MonitorList *list, const char *device_path, const char *ddc_name)
{
    int bus = i2c_bus_number(device_path);
    char *connector = bus >= 0 ? find_drm_connector_for_i2c(bus) : NULL;
    gboolean is_internal = is_internal_connector(connector);
    if (is_internal) {
        g_message("Detected internal display: %s via DRM connector %s", device_path, connector);
    }

    /* Prefer the EDID name: right after a resume ddccontrol often can't identify
     * the monitor yet and reports a generic "VESA standard monitor". */
    char *edid_key = NULL, *edid_name = NULL;
    read_edid_identity(connector, &edid_key, &edid_name);

    const char *name = edid_name ? edid_name : (ddc_name && ddc_name[0] ? ddc_name : NULL);
    const char *type_label = is_internal ? "Internal" : "External";
    char *display_name = name
        ? g_strdup_printf("%s (%s - %s)", name, type_label, device_path)
        : g_strdup_printf("Monitor (%s - %s)", type_label, device_path);

    Monitor *monitor = monitor_new(device_path, display_name);
    monitor_set_internal(monitor, is_internal);
    if (name) monitor_set_model_name(monitor, name);
    if (connector) monitor_set_drm_connector(monitor, connector);
    if (edid_key) monitor_set_config_key(monitor, edid_key);

    monitor_list_add(list, monitor);
    g_message("Found monitor: %s (%s, %s)", device_path, type_label,
              edid_key ? edid_key : "no EDID identity");

    g_free(display_name);
    g_free(edid_key);
    g_free(edid_name);
    g_free(connector);
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
                add_detected_monitor(list, current_device, current_name);
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
        add_detected_monitor(list, current_device, current_name);
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

    /* Check if fork/exec failed */
    if (result == -1) {
        g_warning("Failed to execute 'which' command");
        return FALSE;
    }

    /* Check if command executed successfully and exited with status 0 */
    return (WIFEXITED(result) && WEXITSTATUS(result) == 0);
}