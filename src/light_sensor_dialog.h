/*
 * light_sensor_dialog.h - Light sensor curve configuration dialog interface
 */

#ifndef LIGHT_SENSOR_DIALOG_H
#define LIGHT_SENSOR_DIALOG_H

#include <gtk/gtk.h>
#include "config.h"

G_BEGIN_DECLS

/* Show light sensor curve configuration dialog for a specific monitor */
void show_light_sensor_dialog(GtkWidget *parent, AppConfig *config, const char *device_path, const char *monitor_name);

G_END_DECLS

#endif /* LIGHT_SENSOR_DIALOG_H */
