/*
 * monitor_detect.h - Monitor detection interface
 */

#ifndef MONITOR_DETECT_H
#define MONITOR_DETECT_H

#include "brightness_control.h"

G_BEGIN_DECLS

/* Detect all available DDC/CI monitors */
MonitorList* monitor_detect_all(void);

/* Test if ddccontrol is available */
gboolean monitor_detect_ddccontrol_available(void);

G_END_DECLS

#endif /* MONITOR_DETECT_H */