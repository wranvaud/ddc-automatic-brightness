/*
 * config.c - Configuration management implementation
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Configuration structure */
struct _AppConfig {
    GKeyFile *keyfile;
    char *config_file_path;
    gboolean modified;
};

static const char *CONFIG_GROUP_GENERAL = "General";
static const char *CONFIG_GROUP_MONITORS = "Monitors";
static const char *CONFIG_GROUP_SCHEDULE = "Schedule";

/* Create new configuration */
AppConfig* config_new(void)
{
    AppConfig *config = g_new0(AppConfig, 1);
    
    config->keyfile = g_key_file_new();
    
    /* Determine config file path */
    const char *config_dir = g_get_user_config_dir();
    config->config_file_path = g_build_filename(config_dir, "ddc_automatic_brightness.conf", NULL);
    
    config->modified = FALSE;
    
    return config;
}

/* Free configuration */
void config_free(AppConfig *config)
{
    if (config) {
        if (config->modified) {
            config_save(config);
        }
        
        g_key_file_free(config->keyfile);
        g_free(config->config_file_path);
        g_free(config);
    }
}

/* Load configuration from file */
gboolean config_load(AppConfig *config)
{
    if (!config) {
        return FALSE;
    }
    
    GError *error = NULL;
    gboolean result = g_key_file_load_from_file(config->keyfile, 
                                               config->config_file_path,
                                               G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                               &error);
    
    if (!result) {
        if (error->code != G_FILE_ERROR_NOENT) {
            g_warning("Failed to load config file: %s", error->message);
        }
        g_error_free(error);
        
        /* Set default values */
        config_set_auto_brightness_enabled(config, TRUE);
        config_set_start_minimized(config, FALSE);
        
        return FALSE;
    }
    
    config->modified = FALSE;
    return TRUE;
}

/* Save configuration to file */
gboolean config_save(AppConfig *config)
{
    if (!config) {
        return FALSE;
    }
    
    /* Create config directory if it doesn't exist */
    char *config_dir = g_path_get_dirname(config->config_file_path);
    if (g_mkdir_with_parents(config_dir, 0755) != 0) {
        g_warning("Failed to create config directory: %s", config_dir);
        g_free(config_dir);
        return FALSE;
    }
    g_free(config_dir);
    
    /* Save keyfile */
    GError *error = NULL;
    gboolean result = g_key_file_save_to_file(config->keyfile, 
                                             config->config_file_path,
                                             &error);
    
    if (!result) {
        g_warning("Failed to save config file: %s", error->message);
        g_error_free(error);
        return FALSE;
    }
    
    config->modified = FALSE;
    return TRUE;
}

/* Get default monitor device path */
const char* config_get_default_monitor(AppConfig *config)
{
    if (!config) {
        return NULL;
    }
    
    GError *error = NULL;
    char *value = g_key_file_get_string(config->keyfile, 
                                       CONFIG_GROUP_GENERAL,
                                       "default_monitor",
                                       &error);
    
    if (error) {
        g_error_free(error);
        return NULL;
    }
    
    /* Note: caller should not free this value, it's managed by the keyfile */
    return value;
}

/* Set default monitor device path */
void config_set_default_monitor(AppConfig *config, const char *device_path)
{
    if (!config || !device_path) {
        return;
    }
    
    g_key_file_set_string(config->keyfile,
                         CONFIG_GROUP_GENERAL,
                         "default_monitor",
                         device_path);
    
    config->modified = TRUE;
}

/* Get global auto brightness enabled setting */
gboolean config_get_auto_brightness_enabled(AppConfig *config)
{
    if (!config) {
        return TRUE; /* Default to enabled */
    }
    
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(config->keyfile,
                                           CONFIG_GROUP_GENERAL,
                                           "auto_brightness_enabled",
                                           &error);
    
    if (error) {
        g_error_free(error);
        return TRUE; /* Default to enabled */
    }
    
    return value;
}

/* Set global auto brightness enabled setting */
void config_set_auto_brightness_enabled(AppConfig *config, gboolean enabled)
{
    if (!config) {
        return;
    }
    
    g_key_file_set_boolean(config->keyfile,
                          CONFIG_GROUP_GENERAL,
                          "auto_brightness_enabled",
                          enabled);
    
    config->modified = TRUE;
}

/* Get start minimized setting */
gboolean config_get_start_minimized(AppConfig *config)
{
    if (!config) {
        return FALSE;
    }
    
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(config->keyfile,
                                           CONFIG_GROUP_GENERAL,
                                           "start_minimized",
                                           &error);
    
    if (error) {
        g_error_free(error);
        return FALSE;
    }
    
    return value;
}

/* Set start minimized setting */
void config_set_start_minimized(AppConfig *config, gboolean minimized)
{
    if (!config) {
        return;
    }
    
    g_key_file_set_boolean(config->keyfile,
                          CONFIG_GROUP_GENERAL,
                          "start_minimized",
                          minimized);
    
    config->modified = TRUE;
}

/* Get show brightness in tray setting */
gboolean config_get_show_brightness_in_tray(AppConfig *config)
{
    if (!config) {
        return FALSE;
    }
    
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(config->keyfile,
                                           CONFIG_GROUP_GENERAL,
                                           "show_brightness_in_tray",
                                           &error);
    
    if (error) {
        g_error_free(error);
        return FALSE;  /* Default to not showing brightness in tray */
    }
    
    return value;
}

/* Set show brightness in tray setting */
void config_set_show_brightness_in_tray(AppConfig *config, gboolean show)
{
    if (!config) {
        return;
    }

    g_key_file_set_boolean(config->keyfile,
                          CONFIG_GROUP_GENERAL,
                          "show_brightness_in_tray",
                          show);

    config->modified = TRUE;
}

/* Get show light level in tray setting */
gboolean config_get_show_light_level_in_tray(AppConfig *config)
{
    if (!config) {
        return FALSE;
    }

    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(config->keyfile,
                                           CONFIG_GROUP_GENERAL,
                                           "show_light_level_in_tray",
                                           &error);

    if (error) {
        g_error_free(error);
        return FALSE;  /* Default to not showing light level in tray */
    }

    return value;
}

/* Set show light level in tray setting */
void config_set_show_light_level_in_tray(AppConfig *config, gboolean show)
{
    if (!config) {
        return;
    }

    g_key_file_set_boolean(config->keyfile,
                          CONFIG_GROUP_GENERAL,
                          "show_light_level_in_tray",
                          show);

    config->modified = TRUE;
}

/* Get per-monitor auto brightness setting */
gboolean config_get_monitor_auto_brightness(AppConfig *config, const char *device_path)
{
    if (!config || !device_path) {
        return TRUE; /* Default to enabled */
    }
    
    char *key = g_strdup_printf("%s_auto_brightness", device_path);
    
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(config->keyfile,
                                           CONFIG_GROUP_MONITORS,
                                           key,
                                           &error);
    
    g_free(key);
    
    if (error) {
        g_error_free(error);
        return TRUE; /* Default to enabled */
    }
    
    return value;
}

/* Set per-monitor auto brightness setting */
void config_set_monitor_auto_brightness(AppConfig *config, const char *device_path, gboolean enabled)
{
    if (!config || !device_path) {
        return;
    }
    
    char *key = g_strdup_printf("%s_auto_brightness", device_path);
    
    g_key_file_set_boolean(config->keyfile,
                          CONFIG_GROUP_MONITORS,
                          key,
                          enabled);
    
    g_free(key);
    config->modified = TRUE;
}

/* Get per-monitor auto brightness mode */
AutoBrightnessMode config_get_monitor_auto_brightness_mode(AppConfig *config, const char *device_path)
{
    if (!config || !device_path) {
        return AUTO_BRIGHTNESS_MODE_DISABLED;
    }

    char *key = g_strdup_printf("%s_auto_brightness_mode", device_path);

    GError *error = NULL;
    int value = g_key_file_get_integer(config->keyfile,
                                       CONFIG_GROUP_MONITORS,
                                       key,
                                       &error);

    g_free(key);

    if (error) {
        g_error_free(error);

        /* Check for legacy config: if old auto_brightness was enabled, default to time schedule */
        if (config_get_monitor_auto_brightness(config, device_path)) {
            return AUTO_BRIGHTNESS_MODE_TIME_SCHEDULE;
        }

        return AUTO_BRIGHTNESS_MODE_DISABLED;
    }

    /* Validate mode value */
    if (value < AUTO_BRIGHTNESS_MODE_DISABLED || value > AUTO_BRIGHTNESS_MODE_LAPTOP_DISPLAY) {
        return AUTO_BRIGHTNESS_MODE_DISABLED;
    }

    return (AutoBrightnessMode)value;
}

/* Set per-monitor auto brightness mode */
void config_set_monitor_auto_brightness_mode(AppConfig *config, const char *device_path, AutoBrightnessMode mode)
{
    if (!config || !device_path) {
        return;
    }

    char *key = g_strdup_printf("%s_auto_brightness_mode", device_path);

    g_key_file_set_integer(config->keyfile,
                           CONFIG_GROUP_MONITORS,
                           key,
                           (int)mode);

    g_free(key);

    /* Also update the legacy boolean setting for backward compatibility */
    config_set_monitor_auto_brightness(config, device_path, mode != AUTO_BRIGHTNESS_MODE_DISABLED);

    config->modified = TRUE;
}

/* Get per-monitor brightness offset */
int config_get_monitor_brightness_offset(AppConfig *config, const char *device_path)
{
    if (!config || !device_path) {
        return 0; /* Default to no offset */
    }

    char *key = g_strdup_printf("%s_brightness_offset", device_path);

    GError *error = NULL;
    int value = g_key_file_get_integer(config->keyfile,
                                       CONFIG_GROUP_MONITORS,
                                       key,
                                       &error);

    g_free(key);

    if (error) {
        g_error_free(error);
        return 0; /* Default to no offset */
    }

    /* Clamp value to -20 to +20 range */
    if (value < -20) value = -20;
    if (value > 20) value = 20;

    return value;
}

/* Set per-monitor brightness offset */
void config_set_monitor_brightness_offset(AppConfig *config, const char *device_path, int offset)
{
    if (!config || !device_path) {
        return;
    }

    /* Clamp offset to -20 to +20 range */
    if (offset < -20) offset = -20;
    if (offset > 20) offset = 20;

    char *key = g_strdup_printf("%s_brightness_offset", device_path);

    g_key_file_set_integer(config->keyfile,
                           CONFIG_GROUP_MONITORS,
                           key,
                           offset);

    g_free(key);
    config->modified = TRUE;
}

/* Load light sensor curve for a specific monitor */
gboolean config_load_light_sensor_curve(AppConfig *config, const char *device_path,
                                        LightSensorCurvePoint **points, int *count)
{
    if (!config || !device_path || !points || !count) {
        return FALSE;
    }

    *points = NULL;
    *count = 0;

    /* Build the group name for this monitor's curve */
    char *group = g_strdup_printf("LightSensorCurve_%s", device_path);

    /* Check if this group exists */
    if (!g_key_file_has_group(config->keyfile, group)) {
        g_free(group);
        return FALSE;  /* No curve configured, will use defaults */
    }

    /* Get the number of points */
    GError *error = NULL;
    int num_points = g_key_file_get_integer(config->keyfile, group, "num_points", &error);
    if (error || num_points <= 0) {
        g_clear_error(&error);
        g_free(group);
        return FALSE;
    }

    /* Allocate array for points */
    LightSensorCurvePoint *curve_points = g_new(LightSensorCurvePoint, num_points);

    /* Load each point */
    for (int i = 0; i < num_points; i++) {
        char *lux_key = g_strdup_printf("point_%d_lux", i);
        char *brightness_key = g_strdup_printf("point_%d_brightness", i);

        curve_points[i].lux = g_key_file_get_double(config->keyfile, group, lux_key, &error);
        if (error) {
            g_clear_error(&error);
            g_free(lux_key);
            g_free(brightness_key);
            g_free(curve_points);
            g_free(group);
            return FALSE;
        }

        curve_points[i].brightness = g_key_file_get_integer(config->keyfile, group, brightness_key, &error);
        if (error) {
            g_clear_error(&error);
            g_free(lux_key);
            g_free(brightness_key);
            g_free(curve_points);
            g_free(group);
            return FALSE;
        }

        g_free(lux_key);
        g_free(brightness_key);
    }

    g_free(group);

    *points = curve_points;
    *count = num_points;
    return TRUE;
}

/* Save light sensor curve for a specific monitor */
void config_save_light_sensor_curve(AppConfig *config, const char *device_path,
                                   const LightSensorCurvePoint *points, int count)
{
    if (!config || !device_path || !points || count <= 0) {
        return;
    }

    /* Build the group name for this monitor's curve */
    char *group = g_strdup_printf("LightSensorCurve_%s", device_path);

    /* Remove existing group if present */
    g_key_file_remove_group(config->keyfile, group, NULL);

    /* Save number of points */
    g_key_file_set_integer(config->keyfile, group, "num_points", count);

    /* Save each point */
    for (int i = 0; i < count; i++) {
        char *lux_key = g_strdup_printf("point_%d_lux", i);
        char *brightness_key = g_strdup_printf("point_%d_brightness", i);

        g_key_file_set_double(config->keyfile, group, lux_key, points[i].lux);
        g_key_file_set_integer(config->keyfile, group, brightness_key, points[i].brightness);

        g_free(lux_key);
        g_free(brightness_key);
    }

    g_free(group);
    config->modified = TRUE;
}

/* Get light sensor hysteresis for a monitor (default: 5.0 lux) */
double config_get_light_sensor_hysteresis(AppConfig *config, const char *device_path)
{
    if (!config || !device_path) {
        return 5.0;  /* Default hysteresis */
    }

    /* Build group name for this monitor's light sensor settings */
    char *group = g_strdup_printf("LightSensorCurve_%s", device_path);

    GError *error = NULL;
    double hysteresis = g_key_file_get_double(config->keyfile, group, "hysteresis", &error);

    if (error) {
        /* Not set, use default */
        g_error_free(error);
        hysteresis = 5.0;
    }

    /* Clamp to valid range (0-100 lux) */
    if (hysteresis < 0.0) hysteresis = 0.0;
    if (hysteresis > 100.0) hysteresis = 100.0;

    g_free(group);
    return hysteresis;
}

/* Set light sensor hysteresis for a monitor */
void config_set_light_sensor_hysteresis(AppConfig *config, const char *device_path, double hysteresis)
{
    if (!config || !device_path) {
        return;
    }

    /* Clamp to valid range (0-100 lux) */
    if (hysteresis < 0.0) hysteresis = 0.0;
    if (hysteresis > 100.0) hysteresis = 100.0;

    /* Build group name for this monitor's light sensor settings */
    char *group = g_strdup_printf("LightSensorCurve_%s", device_path);

    g_key_file_set_double(config->keyfile, group, "hysteresis", hysteresis);

    g_free(group);
    config->modified = TRUE;
}

/* Get keyfile for direct access (for schedule configuration) */
GKeyFile* config_get_keyfile(AppConfig *config)
{
    return config ? config->keyfile : NULL;
}