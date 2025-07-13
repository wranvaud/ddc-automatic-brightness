/*
 * config.h - Configuration management interface
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

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

/* Per-monitor settings */
gboolean config_get_monitor_auto_brightness(AppConfig *config, const char *device_path);
void config_set_monitor_auto_brightness(AppConfig *config, const char *device_path, gboolean enabled);

/* Schedule settings */
GKeyFile* config_get_keyfile(AppConfig *config);

G_END_DECLS

#endif /* CONFIG_H */