/*
 * brightness_control.h - Monitor brightness control interface
 */

#ifndef BRIGHTNESS_CONTROL_H
#define BRIGHTNESS_CONTROL_H

#include <glib.h>

G_BEGIN_DECLS

/* Monitor structure */
typedef struct _Monitor Monitor;

/* Monitor list structure */
typedef struct _MonitorList MonitorList;

/* Monitor functions */
Monitor* monitor_new(const char *device_path, const char *name);
void monitor_free(Monitor *monitor);
void monitor_set_internal(Monitor *monitor, gboolean is_internal);

const char* monitor_get_device_path(Monitor *monitor);
const char* monitor_get_display_name(Monitor *monitor);
const char* monitor_get_model_name(Monitor *monitor);
void monitor_set_model_name(Monitor *monitor, const char *model_name);
gboolean monitor_is_internal(const Monitor *monitor);

int monitor_get_brightness(Monitor *monitor);
gboolean monitor_set_brightness(Monitor *monitor, int brightness);
gboolean monitor_is_available(Monitor *monitor);
void monitor_set_available(Monitor *monitor, gboolean available);
gboolean monitor_is_ddc_backoff_active(Monitor *monitor);

/* Brightness tracking for gradual transitions */
int monitor_get_current_brightness(Monitor *monitor);
int monitor_get_target_brightness(Monitor *monitor);
void monitor_set_target_brightness(Monitor *monitor, int brightness);

/* Lux tracking for hysteresis */
double monitor_get_stable_lux(Monitor *monitor);
void monitor_set_stable_lux(Monitor *monitor, double lux);

/* DRM connector path (e.g. "card1-DP-1") for link-status pre-checks */
const char* monitor_get_drm_connector(Monitor *monitor);
void monitor_set_drm_connector(Monitor *monitor, const char *connector);

/* Stable identity used to key per-monitor settings (built from EDID, e.g.
 * "edid-SAM-U32J59x-H4ZMC00130"), so settings follow the monitor when its
 * I2C bus number changes. Falls back to the device path when unknown. */
const char* monitor_get_config_key(Monitor *monitor);
void monitor_set_config_key(Monitor *monitor, const char *key);

/* Enhanced functions with auto-refresh capability */
typedef gboolean (*MonitorRefreshCallback)(void);
int monitor_get_brightness_with_retry(Monitor *monitor, MonitorRefreshCallback refresh_callback);
gboolean monitor_set_brightness_with_retry(Monitor *monitor, int brightness, MonitorRefreshCallback refresh_callback);

/* Monitor list functions */
MonitorList* monitor_list_new(void);
void monitor_list_free(MonitorList *list);

void monitor_list_add(MonitorList *list, Monitor *monitor);
Monitor* monitor_list_get_monitor(MonitorList *list, int index);
int monitor_list_get_count(MonitorList *list);
void monitor_list_sort(MonitorList *list, GCompareFunc compare_func);

G_END_DECLS

#endif /* BRIGHTNESS_CONTROL_H */