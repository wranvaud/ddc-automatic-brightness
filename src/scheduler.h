/*
 * scheduler.h - Brightness scheduling interface
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <glib.h>
#include <gtk/gtk.h>
#include "config.h"

G_BEGIN_DECLS

/* Scheduler structure */
typedef struct _BrightnessScheduler BrightnessScheduler;

/* Schedule entry structure */
typedef struct {
    int hour;
    int minute;
    int brightness;
} ScheduleEntry;

/* Scheduler functions */
BrightnessScheduler* scheduler_new(void);
void scheduler_free(BrightnessScheduler *scheduler);

/* Schedule management */
void scheduler_add_time(BrightnessScheduler *scheduler, int hour, int minute, int brightness);
void scheduler_remove_time(BrightnessScheduler *scheduler, int hour, int minute);
void scheduler_clear(BrightnessScheduler *scheduler);

/* Get current brightness based on time */
int scheduler_get_current_brightness(BrightnessScheduler *scheduler);

/* Get all schedule entries */
GList* scheduler_get_entries(BrightnessScheduler *scheduler);
int scheduler_get_entry_count(BrightnessScheduler *scheduler);

/* Configuration integration */
gboolean scheduler_load_from_config(BrightnessScheduler *scheduler, AppConfig *config);
gboolean scheduler_save_to_config(BrightnessScheduler *scheduler, AppConfig *config);

/* Schedule dialog */
void show_schedule_dialog(GtkWidget *parent, BrightnessScheduler *scheduler, AppConfig *config);

G_END_DECLS

#endif /* SCHEDULER_H */