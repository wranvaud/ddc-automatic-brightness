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

const char* monitor_get_device_path(Monitor *monitor);
const char* monitor_get_display_name(Monitor *monitor);

int monitor_get_brightness(Monitor *monitor);
gboolean monitor_set_brightness(Monitor *monitor, int brightness);
gboolean monitor_is_available(Monitor *monitor);

/* Monitor list functions */
MonitorList* monitor_list_new(void);
void monitor_list_free(MonitorList *list);

void monitor_list_add(MonitorList *list, Monitor *monitor);
Monitor* monitor_list_get_monitor(MonitorList *list, int index);
int monitor_list_get_count(MonitorList *list);

G_END_DECLS

#endif /* BRIGHTNESS_CONTROL_H */