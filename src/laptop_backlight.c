/*
 * laptop_backlight.c - Laptop built-in display backlight reading implementation
 */

#include "laptop_backlight.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

/* Laptop backlight structure */
struct _LaptopBacklight {
    char *device_path;
    int max_brightness;
    gboolean available;
};

/* Detect laptop backlight device */
static gboolean detect_backlight(LaptopBacklight *backlight)
{
    const char *backlight_base = "/sys/class/backlight";
    DIR *dir = opendir(backlight_base);

    if (!dir) {
        g_debug("Cannot open backlight directory");
        return FALSE;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        /* Found a backlight device */
        char device_path[512];
        snprintf(device_path, sizeof(device_path), "%s/%s", backlight_base, entry->d_name);

        /* Check if brightness file exists and is readable */
        char brightness_path[512];
        snprintf(brightness_path, sizeof(brightness_path), "%s/brightness", device_path);

        if (access(brightness_path, R_OK) == 0) {
            /* Read max_brightness */
            char max_brightness_path[512];
            snprintf(max_brightness_path, sizeof(max_brightness_path), "%s/max_brightness", device_path);

            FILE *max_file = fopen(max_brightness_path, "r");
            if (max_file) {
                int max_val;
                if (fscanf(max_file, "%d", &max_val) == 1) {
                    backlight->device_path = g_strdup(device_path);
                    backlight->max_brightness = max_val;
                    fclose(max_file);
                    closedir(dir);
                    return TRUE;
                }
                fclose(max_file);
            }
        }
    }

    closedir(dir);
    return FALSE;
}

/* Create new laptop backlight */
LaptopBacklight* laptop_backlight_new(void)
{
    LaptopBacklight *backlight = g_new0(LaptopBacklight, 1);

    /* Try to detect laptop backlight */
    backlight->available = detect_backlight(backlight);

    if (backlight->available) {
        g_message("Laptop backlight detected at: %s (max: %d)",
                 backlight->device_path, backlight->max_brightness);
    } else {
        g_message("No laptop backlight detected (desktop system?)");
    }

    return backlight;
}

/* Free laptop backlight */
void laptop_backlight_free(LaptopBacklight *backlight)
{
    if (backlight) {
        g_free(backlight->device_path);
        g_free(backlight);
    }
}

/* Check if backlight is available */
gboolean laptop_backlight_is_available(LaptopBacklight *backlight)
{
    return backlight && backlight->available;
}

/* Get device path */
const char* laptop_backlight_get_device_path(LaptopBacklight *backlight)
{
    return backlight ? backlight->device_path : NULL;
}

/* Read current brightness percentage (0-100) */
int laptop_backlight_read_brightness(LaptopBacklight *backlight)
{
    if (!backlight || !backlight->available || !backlight->device_path) {
        return -1;
    }

    char brightness_path[512];
    snprintf(brightness_path, sizeof(brightness_path), "%s/brightness", backlight->device_path);

    FILE *brightness_file = fopen(brightness_path, "r");
    if (!brightness_file) {
        g_warning("Failed to open brightness file: %s", brightness_path);
        return -1;
    }

    int current_brightness;
    if (fscanf(brightness_file, "%d", &current_brightness) != 1) {
        g_warning("Failed to read brightness from: %s", brightness_path);
        fclose(brightness_file);
        return -1;
    }
    fclose(brightness_file);

    /* Convert to percentage */
    int percentage = (current_brightness * 100) / backlight->max_brightness;

    g_debug("Laptop backlight: %d/%d = %d%%", current_brightness, backlight->max_brightness, percentage);

    return percentage;
}
