/*
 * laptop_backlight.h - Laptop built-in display backlight reading interface
 */

#ifndef LAPTOP_BACKLIGHT_H
#define LAPTOP_BACKLIGHT_H

#include <glib.h>

G_BEGIN_DECLS

/* Laptop backlight structure */
typedef struct _LaptopBacklight LaptopBacklight;

/* Laptop backlight functions */
LaptopBacklight* laptop_backlight_new(void);
void laptop_backlight_free(LaptopBacklight *backlight);

/* Check if laptop backlight is available */
gboolean laptop_backlight_is_available(LaptopBacklight *backlight);
const char* laptop_backlight_get_device_path(LaptopBacklight *backlight);

/* Read current brightness percentage (0-100) */
int laptop_backlight_read_brightness(LaptopBacklight *backlight);

G_END_DECLS

#endif /* LAPTOP_BACKLIGHT_H */
