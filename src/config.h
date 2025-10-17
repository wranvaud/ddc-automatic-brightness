/*
 * config.h - Configuration management interface
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>
#include "light_sensor.h"

G_BEGIN_DECLS

/* Configuration structure */
typedef struct _AppConfig AppConfig;

/* Configuration functions */
AppConfig* config_new(void);
void config_free(AppConfig *config);

gboolean config_load(AppConfig *config);
gboolean config_save(AppConfig *config);

/* General settings */
const char* config_get_default_monitor(AppConfig *config);
void config_set_default_monitor(AppConfig *config, const char *device_path);

gboolean config_get_auto_brightness_enabled(AppConfig *config);
void config_set_auto_brightness_enabled(AppConfig *config, gboolean enabled);

gboolean config_get_start_minimized(AppConfig *config);
void config_set_start_minimized(AppConfig *config, gboolean minimized);

gboolean config_get_show_brightness_in_tray(AppConfig *config);
void config_set_show_brightness_in_tray(AppConfig *config, gboolean show);

gboolean config_get_show_light_level_in_tray(AppConfig *config);
void config_set_show_light_level_in_tray(AppConfig *config, gboolean show);

/* Per-monitor settings */
gboolean config_get_monitor_auto_brightness(AppConfig *config, const char *device_path);
void config_set_monitor_auto_brightness(AppConfig *config, const char *device_path, gboolean enabled);

AutoBrightnessMode config_get_monitor_auto_brightness_mode(AppConfig *config, const char *device_path);
void config_set_monitor_auto_brightness_mode(AppConfig *config, const char *device_path, AutoBrightnessMode mode);

int config_get_monitor_brightness_offset(AppConfig *config, const char *device_path);
void config_set_monitor_brightness_offset(AppConfig *config, const char *device_path, int offset);

/* Light sensor curve settings - per monitor */
typedef struct {
    double lux;
    int brightness;  /* 0-100 */
} LightSensorCurvePoint;

gboolean config_load_light_sensor_curve(AppConfig *config, const char *device_path,
                                        LightSensorCurvePoint **points, int *count);
void config_save_light_sensor_curve(AppConfig *config, const char *device_path,
                                   const LightSensorCurvePoint *points, int count);

/* Schedule settings */
GKeyFile* config_get_keyfile(AppConfig *config);

G_END_DECLS

#endif /* CONFIG_H */