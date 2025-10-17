/*
 * light_sensor.h - Ambient light sensor interface
 */

#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

#include <glib.h>

G_BEGIN_DECLS

/* Light sensor structure */
typedef struct _LightSensor LightSensor;

/* Auto brightness mode enumeration */
typedef enum {
    AUTO_BRIGHTNESS_MODE_DISABLED = 0,
    AUTO_BRIGHTNESS_MODE_TIME_SCHEDULE = 1,
    AUTO_BRIGHTNESS_MODE_LIGHT_SENSOR = 2,
    AUTO_BRIGHTNESS_MODE_LAPTOP_DISPLAY = 3
} AutoBrightnessMode;

/* Light sensor functions */
LightSensor* light_sensor_new(void);
void light_sensor_free(LightSensor *sensor);

/* Sensor detection and availability */
gboolean light_sensor_is_available(LightSensor *sensor);
const char* light_sensor_get_device_path(LightSensor *sensor);

/* Read sensor values */
double light_sensor_read_lux(LightSensor *sensor);
gboolean light_sensor_read_raw(LightSensor *sensor, int *raw_value, double *scale);

/* Calculate brightness from ambient light */
int light_sensor_calculate_brightness(LightSensor *sensor, double lux);

/* Calibration settings (legacy function for backward compatibility) */
void light_sensor_set_curve_points(LightSensor *sensor,
                                   double dark_lux, int dark_brightness,
                                   double dim_lux, int dim_brightness,
                                   double normal_lux, int normal_brightness,
                                   double bright_lux, int bright_brightness,
                                   double very_bright_lux, int very_bright_brightness);

/* Set calibration curve from array of points (flexible number of points)
 * points_array should be an array of structures with {double lux; int brightness;} */
void light_sensor_set_curve(LightSensor *sensor, const void *points_array, int count);

G_END_DECLS

#endif /* LIGHT_SENSOR_H */
