/*
 * schedule_dialog.c - Schedule configuration dialog implementation
 */

#include "scheduler.h"
#include <gtk/gtk.h>

/* Dialog data structure */
typedef struct {
    GtkWidget *dialog;
    GtkWidget *schedule_list;
    GtkWidget *hour_spin;
    GtkWidget *minute_spin;
    GtkWidget *brightness_spin;
    
    BrightnessScheduler *scheduler;
    AppConfig *config;
    
    GtkListStore *list_store;
} ScheduleDialogData;

/* Column indices for list store */
enum {
    COL_TIME_STR,
    COL_BRIGHTNESS,
    COL_HOUR,
    COL_MINUTE,
    COL_COUNT
};

/* Forward declarations */
static void refresh_schedule_list(ScheduleDialogData *data);
static void on_add_clicked(GtkButton *button, ScheduleDialogData *data);
static void on_remove_clicked(GtkButton *button, ScheduleDialogData *data);
static void on_save_clicked(GtkButton *button, ScheduleDialogData *data);
static void on_cancel_clicked(GtkButton *button, ScheduleDialogData *data);

/* Show schedule configuration dialog */
void show_schedule_dialog(GtkWidget *parent, BrightnessScheduler *scheduler, AppConfig *config)
{
    ScheduleDialogData *data = g_new0(ScheduleDialogData, 1);
    data->scheduler = scheduler;
    data->config = config;
    
    /* Create dialog */
    data->dialog = gtk_dialog_new_with_buttons("Brightness Schedule Configuration",
                                              GTK_WINDOW(parent),
                                              GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                              (const gchar*)NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(data->dialog), 500, 400);
    
    /* Get dialog content area */
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(data->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);
    
    /* Main container */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);
    
    /* Title label */
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), "<b>Brightness Schedule Configuration</b>");
    gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 0);
    
    /* Schedule list frame */
    GtkWidget *list_frame = gtk_frame_new("Schedule Times");
    gtk_box_pack_start(GTK_BOX(vbox), list_frame, TRUE, TRUE, 0);
    
    /* Create list store */
    data->list_store = gtk_list_store_new(COL_COUNT, 
                                         G_TYPE_STRING,  /* time string */
                                         G_TYPE_INT,     /* brightness */
                                         G_TYPE_INT,     /* hour */
                                         G_TYPE_INT);    /* minute */
    
    /* Create tree view */
    data->schedule_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(data->list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(data->schedule_list), TRUE);
    
    /* Add columns */
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    
    /* Time column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Time", renderer,
                                                     "text", COL_TIME_STR,
                                                     NULL);
    gtk_tree_view_column_set_min_width(column, 80);
    gtk_tree_view_append_column(GTK_TREE_VIEW(data->schedule_list), column);
    
    /* Brightness column */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "editable", TRUE, NULL);
    column = gtk_tree_view_column_new_with_attributes("Brightness (%)", renderer,
                                                     "text", COL_BRIGHTNESS,
                                                     NULL);
    gtk_tree_view_column_set_min_width(column, 120);
    gtk_tree_view_append_column(GTK_TREE_VIEW(data->schedule_list), column);
    
    /* Scrolled window for list */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scrolled), data->schedule_list);
    gtk_container_add(GTK_CONTAINER(list_frame), scrolled);
    
    /* Add/Remove controls frame */
    GtkWidget *controls_frame = gtk_frame_new("Add Schedule Time");
    gtk_box_pack_start(GTK_BOX(vbox), controls_frame, FALSE, FALSE, 0);
    
    GtkWidget *controls_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(controls_hbox), 10);
    gtk_container_add(GTK_CONTAINER(controls_frame), controls_hbox);
    
    /* Time input */
    gtk_box_pack_start(GTK_BOX(controls_hbox), gtk_label_new("Time:"), FALSE, FALSE, 0);
    
    data->hour_spin = gtk_spin_button_new_with_range(0, 23, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->hour_spin), 12);
    gtk_box_pack_start(GTK_BOX(controls_hbox), data->hour_spin, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(controls_hbox), gtk_label_new("h"), FALSE, FALSE, 0);
    
    data->minute_spin = gtk_spin_button_new_with_range(0, 59, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->minute_spin), 0);
    gtk_box_pack_start(GTK_BOX(controls_hbox), data->minute_spin, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(controls_hbox), gtk_label_new("m"), FALSE, FALSE, 0);
    
    /* Brightness input */
    gtk_box_pack_start(GTK_BOX(controls_hbox), gtk_label_new("Brightness:"), FALSE, FALSE, 0);
    
    data->brightness_spin = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->brightness_spin), 50);
    gtk_box_pack_start(GTK_BOX(controls_hbox), data->brightness_spin, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(controls_hbox), gtk_label_new("%"), FALSE, FALSE, 0);
    
    /* Add/Remove buttons */
    GtkWidget *add_button = gtk_button_new_with_label("Add");
    gtk_box_pack_start(GTK_BOX(controls_hbox), add_button, FALSE, FALSE, 0);
    g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_clicked), data);
    
    GtkWidget *remove_button = gtk_button_new_with_label("Remove Selected");
    gtk_box_pack_start(GTK_BOX(controls_hbox), remove_button, FALSE, FALSE, 0);
    g_signal_connect(remove_button, "clicked", G_CALLBACK(on_remove_clicked), data);
    
    /* Dialog buttons */
    GtkWidget *save_button = gtk_button_new_with_label("Save");
    GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
    
    gtk_dialog_add_action_widget(GTK_DIALOG(data->dialog), cancel_button, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_action_widget(GTK_DIALOG(data->dialog), save_button, GTK_RESPONSE_OK);
    
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), data);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), data);
    
    /* Populate list with current schedule */
    refresh_schedule_list(data);
    
    /* Show dialog */
    gtk_widget_show_all(data->dialog);
    
    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(data->dialog));
    
    /* Cleanup */
    gtk_widget_destroy(data->dialog);
    g_free(data);
}

/* Refresh the schedule list */
static void refresh_schedule_list(ScheduleDialogData *data)
{
    gtk_list_store_clear(data->list_store);
    
    GList *entries = scheduler_get_entries(data->scheduler);
    
    for (GList *item = entries; item; item = item->next) {
        ScheduleEntry *entry = (ScheduleEntry*)item->data;
        
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", entry->hour, entry->minute);
        
        GtkTreeIter iter;
        gtk_list_store_append(data->list_store, &iter);
        gtk_list_store_set(data->list_store, &iter,
                          COL_TIME_STR, time_str,
                          COL_BRIGHTNESS, entry->brightness,
                          COL_HOUR, entry->hour,
                          COL_MINUTE, entry->minute,
                          -1);
    }
}

/* Add button clicked */
static void on_add_clicked(GtkButton *button, ScheduleDialogData *data)
{
    int hour = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->hour_spin));
    int minute = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->minute_spin));
    int brightness = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->brightness_spin));
    
    /* Check for duplicate time */
    GList *entries = scheduler_get_entries(data->scheduler);
    for (GList *item = entries; item; item = item->next) {
        ScheduleEntry *entry = (ScheduleEntry*)item->data;
        if (entry->hour == hour && entry->minute == minute) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                      GTK_DIALOG_MODAL,
                                                      GTK_MESSAGE_WARNING,
                                                      GTK_BUTTONS_OK,
                                                      "Time %02d:%02d is already in the schedule.",
                                                      hour, minute);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }
    }
    
    /* Add to scheduler */
    scheduler_add_time(data->scheduler, hour, minute, brightness);
    
    /* Refresh list */
    refresh_schedule_list(data);
}

/* Remove button clicked */
static void on_remove_clicked(GtkButton *button, ScheduleDialogData *data)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data->schedule_list));
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        int hour, minute;
        gtk_tree_model_get(model, &iter,
                          COL_HOUR, &hour,
                          COL_MINUTE, &minute,
                          -1);
        
        /* Remove from scheduler */
        scheduler_remove_time(data->scheduler, hour, minute);
        
        /* Refresh list */
        refresh_schedule_list(data);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_INFO,
                                                  GTK_BUTTONS_OK,
                                                  "Please select a time to remove.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

/* Save button clicked */
static void on_save_clicked(GtkButton *button, ScheduleDialogData *data)
{
    if (scheduler_get_entry_count(data->scheduler) == 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Please add at least one time to the schedule.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    /* Save to configuration */
    if (scheduler_save_to_config(data->scheduler, data->config)) {
        config_save(data->config);
        gtk_dialog_response(GTK_DIALOG(data->dialog), GTK_RESPONSE_OK);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Failed to save schedule configuration.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

/* Cancel button clicked */
static void on_cancel_clicked(GtkButton *button, ScheduleDialogData *data)
{
    gtk_dialog_response(GTK_DIALOG(data->dialog), GTK_RESPONSE_CANCEL);
}