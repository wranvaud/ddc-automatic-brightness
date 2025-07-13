/*
 * scheduler.c - Brightness scheduling implementation
 */

#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* Scheduler structure */
struct _BrightnessScheduler {
    GList *entries;
};

/* Create new scheduler */
BrightnessScheduler* scheduler_new(void)
{
    BrightnessScheduler *scheduler = g_new0(BrightnessScheduler, 1);
    scheduler->entries = NULL;
    return scheduler;
}

/* Free scheduler */
void scheduler_free(BrightnessScheduler *scheduler)
{
    if (scheduler) {
        g_list_free_full(scheduler->entries, g_free);
        g_free(scheduler);
    }
}

/* Compare function for sorting schedule entries */
static gint schedule_entry_compare(gconstpointer a, gconstpointer b)
{
    const ScheduleEntry *entry_a = (const ScheduleEntry*)a;
    const ScheduleEntry *entry_b = (const ScheduleEntry*)b;
    
    int time_a = entry_a->hour * 60 + entry_a->minute;
    int time_b = entry_b->hour * 60 + entry_b->minute;
    
    return time_a - time_b;
}

/* Add time to schedule */
void scheduler_add_time(BrightnessScheduler *scheduler, int hour, int minute, int brightness)
{
    if (!scheduler || hour < 0 || hour > 23 || minute < 0 || minute > 59 || 
        brightness < 0 || brightness > 100) {
        return;
    }
    
    /* Check if entry already exists */
    GList *item = scheduler->entries;
    while (item) {
        ScheduleEntry *entry = (ScheduleEntry*)item->data;
        if (entry->hour == hour && entry->minute == minute) {
            /* Update existing entry */
            entry->brightness = brightness;
            return;
        }
        item = item->next;
    }
    
    /* Create new entry */
    ScheduleEntry *entry = g_new(ScheduleEntry, 1);
    entry->hour = hour;
    entry->minute = minute;
    entry->brightness = brightness;
    
    /* Insert in sorted order */
    scheduler->entries = g_list_insert_sorted(scheduler->entries, entry, schedule_entry_compare);
}

/* Remove time from schedule */
void scheduler_remove_time(BrightnessScheduler *scheduler, int hour, int minute)
{
    if (!scheduler) {
        return;
    }
    
    GList *item = scheduler->entries;
    while (item) {
        ScheduleEntry *entry = (ScheduleEntry*)item->data;
        if (entry->hour == hour && entry->minute == minute) {
            scheduler->entries = g_list_delete_link(scheduler->entries, item);
            g_free(entry);
            return;
        }
        item = item->next;
    }
}

/* Clear all schedule entries */
void scheduler_clear(BrightnessScheduler *scheduler)
{
    if (scheduler) {
        g_list_free_full(scheduler->entries, g_free);
        scheduler->entries = NULL;
    }
}

/* Get current brightness based on time */
int scheduler_get_current_brightness(BrightnessScheduler *scheduler)
{
    if (!scheduler || !scheduler->entries) {
        return -1;
    }
    
    /* Get current time */
    time_t now_time = time(NULL);
    struct tm *now_tm = localtime(&now_time);
    int current_minutes = now_tm->tm_hour * 60 + now_tm->tm_min;
    
    GList *entries = scheduler->entries;
    int count = g_list_length(entries);
    
    if (count == 0) {
        return -1;
    }
    
    if (count == 1) {
        ScheduleEntry *entry = (ScheduleEntry*)entries->data;
        return entry->brightness;
    }
    
    /* Find appropriate brightness value */
    for (GList *item = entries; item; item = item->next) {
        ScheduleEntry *entry = (ScheduleEntry*)item->data;
        int entry_minutes = entry->hour * 60 + entry->minute;
        
        if (current_minutes <= entry_minutes) {
            if (item == entries) {
                /* Before first entry, use last entry from previous day */
                GList *last = g_list_last(entries);
                ScheduleEntry *last_entry = (ScheduleEntry*)last->data;
                return last_entry->brightness;
            } else {
                /* Interpolate between previous and current entry */
                GList *prev_item = item->prev;
                ScheduleEntry *prev_entry = (ScheduleEntry*)prev_item->data;
                
                int prev_minutes = prev_entry->hour * 60 + prev_entry->minute;
                int next_minutes = entry_minutes;
                
                /* Linear interpolation */
                double ratio = (double)(current_minutes - prev_minutes) / 
                              (double)(next_minutes - prev_minutes);
                
                int brightness = (int)(prev_entry->brightness + 
                                     ratio * (entry->brightness - prev_entry->brightness));
                
                return brightness;
            }
        }
    }
    
    /* After last entry, use last brightness */
    GList *last = g_list_last(entries);
    ScheduleEntry *last_entry = (ScheduleEntry*)last->data;
    return last_entry->brightness;
}

/* Get all schedule entries */
GList* scheduler_get_entries(BrightnessScheduler *scheduler)
{
    return scheduler ? scheduler->entries : NULL;
}

/* Get entry count */
int scheduler_get_entry_count(BrightnessScheduler *scheduler)
{
    return scheduler ? g_list_length(scheduler->entries) : 0;
}

/* Load schedule from configuration */
gboolean scheduler_load_from_config(BrightnessScheduler *scheduler, AppConfig *config)
{
    if (!scheduler || !config) {
        return FALSE;
    }
    
    GKeyFile *keyfile = config_get_keyfile(config);
    if (!keyfile) {
        return FALSE;
    }
    
    /* Clear existing schedule */
    scheduler_clear(scheduler);
    
    /* Load schedule entries */
    GError *error = NULL;
    gchar **keys = g_key_file_get_keys(keyfile, "Schedule", NULL, &error);
    
    if (error) {
        g_error_free(error);
        return FALSE;
    }
    
    if (!keys) {
        return FALSE;
    }
    
    for (int i = 0; keys[i]; i++) {
        /* Parse key in format "HH:MM" */
        int hour, minute;
        if (sscanf(keys[i], "%d:%d", &hour, &minute) == 2) {
            GError *value_error = NULL;
            int brightness = g_key_file_get_integer(keyfile, "Schedule", keys[i], &value_error);
            
            if (!value_error && brightness >= 0 && brightness <= 100) {
                scheduler_add_time(scheduler, hour, minute, brightness);
            }
            
            if (value_error) {
                g_error_free(value_error);
            }
        }
    }
    
    g_strfreev(keys);
    return (scheduler_get_entry_count(scheduler) > 0);
}

/* Save schedule to configuration */
gboolean scheduler_save_to_config(BrightnessScheduler *scheduler, AppConfig *config)
{
    if (!scheduler || !config) {
        return FALSE;
    }
    
    GKeyFile *keyfile = config_get_keyfile(config);
    if (!keyfile) {
        return FALSE;
    }
    
    /* Remove existing schedule group */
    g_key_file_remove_group(keyfile, "Schedule", NULL);
    
    /* Save each entry */
    for (GList *item = scheduler->entries; item; item = item->next) {
        ScheduleEntry *entry = (ScheduleEntry*)item->data;
        
        char key[16];
        snprintf(key, sizeof(key), "%02d:%02d", entry->hour, entry->minute);
        
        g_key_file_set_integer(keyfile, "Schedule", key, entry->brightness);
    }
    
    return TRUE;
}