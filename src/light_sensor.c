/*
 * light_sensor.c - Ambient light sensor implementation
 */

#include "light_sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <math.h>

/* Light sensor structure */
struct _LightSensor {
    char *device_path;
    gboolean available;

    /* Calibration curve points (lux -> brightness %) */
    struct {
        double lux;
        int brightness;
    } *curve_points;
    int num_curve_points;
};

/* Default brightness curve mapping lux to brightness percentage */
static void set_default_curve(LightSensor *sensor)
{
    /* Conservative default curve with 5 points */
    sensor->num_curve_points = 5;

    /* Anonymous struct type for curve points */
    typedef struct {
        double lux;
        int brightness;
    } CurvePoint;

    sensor->curve_points = g_new(CurvePoint, sensor->num_curve_points);

    /* Point 0: Dark (0 lux) -> 20% (conservative, not too dark) */
    sensor->curve_points[0].lux = 0.0;
    sensor->curve_points[0].brightness = 20;

    /* Point 1: Dim (50 lux) -> 40% */
    sensor->curve_points[1].lux = 50.0;
    sensor->curve_points[1].brightness = 40;

    /* Point 2: Normal indoor (200 lux) -> 70% */
    sensor->curve_points[2].lux = 200.0;
    sensor->curve_points[2].brightness = 70;

    /* Point 3: Bright (500 lux) -> 90% */
    sensor->curve_points[3].lux = 500.0;
    sensor->curve_points[3].brightness = 90;

    /* Point 4: Very bright (1000 lux) -> 100% */
    sensor->curve_points[4].lux = 1000.0;
    sensor->curve_points[4].brightness = 100;
}

/* Detect and open light sensor device */
static gboolean detect_light_sensor(LightSensor *sensor)
{
    const char *iio_base = "/sys/bus/iio/devices";
    DIR *dir = opendir(iio_base);

    if (!dir) {
        g_warning("Cannot open IIO devices directory");
        return FALSE;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0) {
            continue;
        }

        /* Check if this device has illuminance capability */
        char name_path[512];
        snprintf(name_path, sizeof(name_path), "%s/%s/name", iio_base, entry->d_name);

        FILE *name_file = fopen(name_path, "r");
        if (name_file) {
            char name[128];
            if (fgets(name, sizeof(name), name_file)) {
                /* Remove newline */
                name[strcspn(name, "\n")] = 0;

                /* Check if this is an ambient light sensor */
                if (strcmp(name, "als") == 0 || strstr(name, "light") || strstr(name, "als")) {
                    fclose(name_file);

                    /* Verify illuminance attribute exists */
                    char illum_path[512];
                    snprintf(illum_path, sizeof(illum_path), "%s/%s/in_illuminance_raw",
                            iio_base, entry->d_name);

                    if (access(illum_path, R_OK) == 0) {
                        /* Found a valid ambient light sensor */
                        sensor->device_path = g_strdup_printf("%s/%s", iio_base, entry->d_name);
                        closedir(dir);
                        return TRUE;
                    }
                }
            }
            fclose(name_file);
        }
    }

    closedir(dir);
    return FALSE;
}

/* Create new light sensor */
LightSensor* light_sensor_new(void)
{
    LightSensor *sensor = g_new0(LightSensor, 1);

    /* Set default calibration curve */
    set_default_curve(sensor);

    /* Try to detect light sensor */
    sensor->available = detect_light_sensor(sensor);

    if (sensor->available) {
        g_message("Light sensor detected at: %s", sensor->device_path);
    } else {
        g_message("No ambient light sensor detected");
    }

    return sensor;
}

/* Free light sensor */
void light_sensor_free(LightSensor *sensor)
{
    if (sensor) {
        g_free(sensor->device_path);
        g_free(sensor->curve_points);
        g_free(sensor);
    }
}

/* Check if sensor is available */
gboolean light_sensor_is_available(LightSensor *sensor)
{
    return sensor && sensor->available;
}

/* Get device path */
const char* light_sensor_get_device_path(LightSensor *sensor)
{
    return sensor ? sensor->device_path : NULL;
}

/* Read raw sensor value and scale */
gboolean light_sensor_read_raw(LightSensor *sensor, int *raw_value, double *scale)
{
    if (!sensor || !sensor->available || !sensor->device_path) {
        return FALSE;
    }

    /* Read raw value */
    char raw_path[512];
    snprintf(raw_path, sizeof(raw_path), "%s/in_illuminance_raw", sensor->device_path);

    FILE *raw_file = fopen(raw_path, "r");
    if (!raw_file) {
        g_warning("Failed to open raw file: %s", raw_path);
        return FALSE;
    }

    int raw;
    if (fscanf(raw_file, "%d", &raw) != 1) {
        g_warning("Failed to read raw value from: %s", raw_path);
        fclose(raw_file);
        return FALSE;
    }
    fclose(raw_file);
    g_debug("Read raw value: %d from %s", raw, raw_path);

    /* Read scale */
    char scale_path[512];
    snprintf(scale_path, sizeof(scale_path), "%s/in_illuminance_scale", sensor->device_path);

    FILE *scale_file = fopen(scale_path, "r");
    if (!scale_file) {
        /* Scale might not exist, default to 1.0 */
        g_debug("Scale file not found, using default 1.0: %s", scale_path);
        if (raw_value) *raw_value = raw;
        if (scale) *scale = 1.0;
        return TRUE;
    }

    /* Read scale as string to avoid locale issues with fscanf */
    char scale_str[64];
    if (!fgets(scale_str, sizeof(scale_str), scale_file)) {
        g_warning("Failed to read scale value from %s, using 1.0", scale_path);
        fclose(scale_file);
        if (raw_value) *raw_value = raw;
        if (scale) *scale = 1.0;
        return TRUE;
    }
    fclose(scale_file);

    /* Use strtod which is locale-independent for parsing */
    char *endptr;
    double scale_val = g_ascii_strtod(scale_str, &endptr);

    if (endptr == scale_str || scale_val == 0.0) {
        /* Failed to parse, default to 1.0 */
        g_warning("Failed to parse scale value '%s' from %s, using 1.0", scale_str, scale_path);
        if (raw_value) *raw_value = raw;
        if (scale) *scale = 1.0;
        return TRUE;
    }

    g_debug("Read scale value: %f from %s", scale_val, scale_path);

    if (raw_value) *raw_value = raw;
    if (scale) *scale = scale_val;

    return TRUE;
}

/* Read sensor value in lux */
double light_sensor_read_lux(LightSensor *sensor)
{
    int raw;
    double scale;

    if (!light_sensor_read_raw(sensor, &raw, &scale)) {
        return -1.0;
    }

    double lux = (double)raw * scale;
    g_debug("Sensor read: raw=%d, scale=%f, lux=%f", raw, scale, lux);
    return lux;
}

/* Linear interpolation helper */
static int interpolate(double x, double x1, double x2, int y1, int y2)
{
    if (x <= x1) return y1;
    if (x >= x2) return y2;

    double ratio = (x - x1) / (x2 - x1);
    return (int)(y1 + ratio * (y2 - y1));
}

/* Calculate brightness percentage from lux value using calibration curve */
int light_sensor_calculate_brightness(LightSensor *sensor, double lux)
{
    if (!sensor || lux < 0 || !sensor->curve_points || sensor->num_curve_points < 2) {
        return -1;
    }

    /* If below the first point, return first point's brightness */
    if (lux <= sensor->curve_points[0].lux) {
        return sensor->curve_points[0].brightness;
    }

    /* If beyond the last point, return last point's brightness */
    if (lux >= sensor->curve_points[sensor->num_curve_points - 1].lux) {
        return sensor->curve_points[sensor->num_curve_points - 1].brightness;
    }

    /* Find the appropriate segment in the curve */
    for (int i = 0; i < sensor->num_curve_points - 1; i++) {
        if (lux <= sensor->curve_points[i + 1].lux) {
            /* Interpolate between point i and i+1 */
            return interpolate(lux,
                             sensor->curve_points[i].lux,
                             sensor->curve_points[i + 1].lux,
                             sensor->curve_points[i].brightness,
                             sensor->curve_points[i + 1].brightness);
        }
    }

    /* Fallback: return last point's brightness */
    return sensor->curve_points[sensor->num_curve_points - 1].brightness;
}

/* Set custom calibration curve (legacy function kept for backward compatibility) */
void light_sensor_set_curve_points(LightSensor *sensor,
                                   double dark_lux, int dark_brightness,
                                   double dim_lux, int dim_brightness,
                                   double normal_lux, int normal_brightness,
                                   double bright_lux, int bright_brightness,
                                   double very_bright_lux, int very_bright_brightness)
{
    if (!sensor) return;

    /* Free old curve if exists */
    g_free(sensor->curve_points);

    /* Allocate new curve with 5 points */
    sensor->num_curve_points = 5;

    /* Anonymous struct type for curve points */
    typedef struct {
        double lux;
        int brightness;
    } CurvePoint;

    sensor->curve_points = g_new(CurvePoint, sensor->num_curve_points);

    sensor->curve_points[0].lux = dark_lux;
    sensor->curve_points[0].brightness = dark_brightness;

    sensor->curve_points[1].lux = dim_lux;
    sensor->curve_points[1].brightness = dim_brightness;

    sensor->curve_points[2].lux = normal_lux;
    sensor->curve_points[2].brightness = normal_brightness;

    sensor->curve_points[3].lux = bright_lux;
    sensor->curve_points[3].brightness = bright_brightness;

    sensor->curve_points[4].lux = very_bright_lux;
    sensor->curve_points[4].brightness = very_bright_brightness;
}

/* Set calibration curve from array of points (flexible number of points) */
void light_sensor_set_curve(LightSensor *sensor, const void *points_array, int count)
{
    if (!sensor || !points_array || count < 2) {
        return;
    }

    /* Free old curve if exists */
    g_free(sensor->curve_points);

    /* Allocate new curve */
    sensor->num_curve_points = count;

    /* Anonymous struct type for curve points */
    typedef struct {
        double lux;
        int brightness;
    } CurvePoint;

    sensor->curve_points = g_new(CurvePoint, count);

    /* Copy points from input array
     * The input array should have the same structure (double lux, int brightness) */
    const CurvePoint *input_points = (const CurvePoint *)points_array;
    for (int i = 0; i < count; i++) {
        sensor->curve_points[i].lux = input_points[i].lux;
        sensor->curve_points[i].brightness = input_points[i].brightness;
    }
}

