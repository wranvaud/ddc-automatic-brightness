/*
 * light_sensor_dialog.c - Light sensor curve configuration dialog implementation
 */

#define _USE_MATH_DEFINES
#include "light_sensor_dialog.h"
#include "config.h"
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Define M_PI if not available */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Dialog data structure */
typedef struct {
    GtkWidget *dialog;
    GtkWidget *curve_list;
    GtkWidget *lux_spin;
    GtkWidget *brightness_spin;
    GtkWidget *graph_drawing_area;

    AppConfig *config;
    const char *device_path;
    const char *monitor_name;

    GtkListStore *list_store;

    /* Curve data */
    LightSensorCurvePoint *points;
    int point_count;
} LightSensorDialogData;

/* Column indices for list store */
enum {
    COL_LUX,
    COL_BRIGHTNESS,
    COL_COUNT
};

/* Forward declarations */
static void refresh_curve_list(LightSensorDialogData *data);
static void on_add_clicked(GtkButton *button, LightSensorDialogData *data);
static void on_remove_clicked(GtkButton *button, LightSensorDialogData *data);
static void on_reset_defaults_clicked(GtkButton *button, LightSensorDialogData *data);
static void on_save_clicked(GtkButton *button, LightSensorDialogData *data);
static void on_cancel_clicked(GtkButton *button, LightSensorDialogData *data);
static void on_lux_edited(GtkCellRendererText *renderer, gchar *path, gchar *new_text, LightSensorDialogData *data);
static void on_brightness_edited(GtkCellRendererText *renderer, gchar *path, gchar *new_text, LightSensorDialogData *data);
static void lux_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data);
static gboolean on_graph_draw(GtkWidget *widget, cairo_t *cr, LightSensorDialogData *data);
static void redraw_graph(LightSensorDialogData *data);
static void load_curve_from_config(LightSensorDialogData *data);
static void set_default_curve(LightSensorDialogData *data);

/* Show light sensor curve configuration dialog */
void show_light_sensor_dialog(GtkWidget *parent, AppConfig *config, const char *device_path, const char *monitor_name)
{
    LightSensorDialogData *data = g_new0(LightSensorDialogData, 1);
    data->config = config;
    data->device_path = g_strdup(device_path);
    data->monitor_name = g_strdup(monitor_name);

    /* Load existing curve or use defaults */
    load_curve_from_config(data);

    /* Create dialog */
    char title[256];
    snprintf(title, sizeof(title), "Light Sensor Curve - %s", monitor_name);
    data->dialog = gtk_dialog_new_with_buttons(title,
                                              GTK_WINDOW(parent),
                                              GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                              (const gchar*)NULL);

    gtk_window_set_default_size(GTK_WINDOW(data->dialog), 600, 550);
    gtk_window_set_resizable(GTK_WINDOW(data->dialog), TRUE);

    /* Get dialog content area */
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(data->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Main container */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    /* Graph frame - fixed height */
    GtkWidget *graph_frame = gtk_frame_new("Brightness Curve");
    gtk_box_pack_start(GTK_BOX(vbox), graph_frame, FALSE, FALSE, 0);

    /* Drawing area for curve graph */
    data->graph_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(data->graph_drawing_area, -1, 200);
    g_signal_connect(data->graph_drawing_area, "draw", G_CALLBACK(on_graph_draw), data);
    gtk_container_add(GTK_CONTAINER(graph_frame), data->graph_drawing_area);

    /* Curve points list frame - expandable */
    GtkWidget *list_frame = gtk_frame_new("Curve Points");
    gtk_widget_set_vexpand(list_frame, TRUE);
    gtk_widget_set_hexpand(list_frame, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), list_frame, TRUE, TRUE, 0);

    /* Create list store */
    data->list_store = gtk_list_store_new(COL_COUNT,
                                         G_TYPE_DOUBLE,  /* lux */
                                         G_TYPE_INT);    /* brightness */

    /* Create tree view */
    data->curve_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(data->list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(data->curve_list), TRUE);
    gtk_widget_set_vexpand(data->curve_list, TRUE);
    gtk_widget_set_hexpand(data->curve_list, TRUE);

    /* Add columns */
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    /* Lux column */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "editable", TRUE, NULL);
    column = gtk_tree_view_column_new_with_attributes("Ambient Light (lux)", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, lux_cell_data_func, NULL, NULL);
    gtk_tree_view_column_set_min_width(column, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(data->curve_list), column);
    g_signal_connect(renderer, "edited", G_CALLBACK(on_lux_edited), data);

    /* Brightness column */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "editable", TRUE, NULL);
    column = gtk_tree_view_column_new_with_attributes("Brightness (%)", renderer,
                                                     "text", COL_BRIGHTNESS,
                                                     NULL);
    gtk_tree_view_column_set_min_width(column, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(data->curve_list), column);
    g_signal_connect(renderer, "edited", G_CALLBACK(on_brightness_edited), data);

    /* Scrolled window for list */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
    gtk_widget_set_size_request(scrolled, -1, 140);  /* Minimum height to show at least 4 rows */
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);

    gtk_container_add(GTK_CONTAINER(scrolled), data->curve_list);
    gtk_container_add(GTK_CONTAINER(list_frame), scrolled);

    /* Button row below the list */
    GtkWidget *button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), button_hbox, FALSE, FALSE, 5);

    GtkWidget *remove_button = gtk_button_new_with_label("Remove Selected");
    gtk_box_pack_start(GTK_BOX(button_hbox), remove_button, FALSE, FALSE, 0);
    g_signal_connect(remove_button, "clicked", G_CALLBACK(on_remove_clicked), data);

    GtkWidget *reset_button = gtk_button_new_with_label("Reset to Defaults");
    gtk_box_pack_start(GTK_BOX(button_hbox), reset_button, FALSE, FALSE, 0);
    g_signal_connect(reset_button, "clicked", G_CALLBACK(on_reset_defaults_clicked), data);

    /* Add controls frame - fixed at bottom */
    GtkWidget *controls_frame = gtk_frame_new("Add Curve Point");
    gtk_box_pack_start(GTK_BOX(vbox), controls_frame, FALSE, FALSE, 0);

    GtkWidget *controls_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(controls_vbox), 10);
    gtk_container_add(GTK_CONTAINER(controls_frame), controls_vbox);

    /* Lux input line */
    GtkWidget *lux_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(controls_vbox), lux_hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(lux_hbox), gtk_label_new("Ambient Light:"), FALSE, FALSE, 0);

    data->lux_spin = gtk_spin_button_new_with_range(0, 10000, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->lux_spin), 100);
    gtk_box_pack_start(GTK_BOX(lux_hbox), data->lux_spin, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(lux_hbox), gtk_label_new("lux"), FALSE, FALSE, 0);

    /* Brightness input line */
    GtkWidget *brightness_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(controls_vbox), brightness_hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(brightness_hbox), gtk_label_new("Brightness:"), FALSE, FALSE, 0);

    data->brightness_spin = gtk_spin_button_new_with_range(0, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(data->brightness_spin), 50);
    gtk_box_pack_start(GTK_BOX(brightness_hbox), data->brightness_spin, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(brightness_hbox), gtk_label_new("%"), FALSE, FALSE, 0);

    /* Add button line */
    GtkWidget *add_button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(controls_vbox), add_button_hbox, FALSE, FALSE, 0);

    GtkWidget *add_button = gtk_button_new_with_label("Add");
    gtk_box_pack_start(GTK_BOX(add_button_hbox), add_button, FALSE, FALSE, 0);
    g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_clicked), data);

    /* Dialog buttons */
    GtkWidget *save_button = gtk_button_new_with_label("Save");
    GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");

    gtk_dialog_add_action_widget(GTK_DIALOG(data->dialog), cancel_button, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_action_widget(GTK_DIALOG(data->dialog), save_button, GTK_RESPONSE_OK);

    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), data);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), data);

    /* Populate list with current curve */
    refresh_curve_list(data);

    /* Show dialog */
    gtk_widget_show_all(data->dialog);

    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(data->dialog));

    /* Cleanup */
    gtk_widget_destroy(data->dialog);
    g_free((char*)data->device_path);
    g_free((char*)data->monitor_name);
    g_free(data->points);
    g_free(data);
}

/* Load curve from configuration */
static void load_curve_from_config(LightSensorDialogData *data)
{
    LightSensorCurvePoint *points = NULL;
    int count = 0;

    if (config_load_light_sensor_curve(data->config, data->device_path, &points, &count)) {
        data->points = points;
        data->point_count = count;
    } else {
        /* Use default curve */
        set_default_curve(data);
    }
}

/* Set default curve */
static void set_default_curve(LightSensorDialogData *data)
{
    g_free(data->points);

    /* Conservative default curve with 5 points */
    data->point_count = 5;
    data->points = g_new(LightSensorCurvePoint, data->point_count);

    data->points[0].lux = 0.0;
    data->points[0].brightness = 20;

    data->points[1].lux = 50.0;
    data->points[1].brightness = 40;

    data->points[2].lux = 200.0;
    data->points[2].brightness = 70;

    data->points[3].lux = 500.0;
    data->points[3].brightness = 90;

    data->points[4].lux = 1000.0;
    data->points[4].brightness = 100;
}

/* Format lux values as integers in the tree view */
static void lux_cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer,
                               GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
    (void)col;
    (void)user_data;

    double lux;
    gtk_tree_model_get(model, iter, COL_LUX, &lux, -1);

    /* Format as integer */
    char text[32];
    snprintf(text, sizeof(text), "%.0f", lux);
    g_object_set(renderer, "text", text, NULL);
}

/* Refresh the curve list */
static void refresh_curve_list(LightSensorDialogData *data)
{
    gtk_list_store_clear(data->list_store);

    for (int i = 0; i < data->point_count; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(data->list_store, &iter);
        gtk_list_store_set(data->list_store, &iter,
                          COL_LUX, data->points[i].lux,
                          COL_BRIGHTNESS, data->points[i].brightness,
                          -1);
    }

    /* Redraw graph */
    redraw_graph(data);
}

/* Comparison function for sorting curve points by lux */
static int compare_lux(const void *a, const void *b)
{
    const LightSensorCurvePoint *pa = (const LightSensorCurvePoint *)a;
    const LightSensorCurvePoint *pb = (const LightSensorCurvePoint *)b;

    if (pa->lux < pb->lux) return -1;
    if (pa->lux > pb->lux) return 1;
    return 0;
}

/* Add button clicked */
static void on_add_clicked(GtkButton *button, LightSensorDialogData *data)
{
    (void)button;

    double lux = gtk_spin_button_get_value(GTK_SPIN_BUTTON(data->lux_spin));
    int brightness = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->brightness_spin));

    /* Check for duplicate lux value */
    for (int i = 0; i < data->point_count; i++) {
        if (fabs(data->points[i].lux - lux) < 0.01) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                      GTK_DIALOG_MODAL,
                                                      GTK_MESSAGE_WARNING,
                                                      GTK_BUTTONS_OK,
                                                      "Light level %.1f lux is already in the curve.",
                                                      lux);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }
    }

    /* Add new point */
    data->point_count++;
    data->points = g_renew(LightSensorCurvePoint, data->points, data->point_count);
    data->points[data->point_count - 1].lux = lux;
    data->points[data->point_count - 1].brightness = brightness;

    /* Sort points by lux */
    qsort(data->points, data->point_count, sizeof(LightSensorCurvePoint), compare_lux);

    /* Refresh list and graph */
    refresh_curve_list(data);
}

/* Remove button clicked */
static void on_remove_clicked(GtkButton *button, LightSensorDialogData *data)
{
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data->curve_list));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        if (data->point_count <= 2) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                      GTK_DIALOG_MODAL,
                                                      GTK_MESSAGE_WARNING,
                                                      GTK_BUTTONS_OK,
                                                      "Cannot remove point. Curve must have at least 2 points.");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }

        double lux;
        gtk_tree_model_get(model, &iter, COL_LUX, &lux, -1);

        /* Find and remove point */
        for (int i = 0; i < data->point_count; i++) {
            if (fabs(data->points[i].lux - lux) < 0.01) {
                /* Shift remaining points */
                for (int j = i; j < data->point_count - 1; j++) {
                    data->points[j] = data->points[j + 1];
                }
                data->point_count--;
                break;
            }
        }

        /* Refresh list and graph */
        refresh_curve_list(data);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_INFO,
                                                  GTK_BUTTONS_OK,
                                                  "Please select a point to remove.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

/* Reset defaults button clicked */
static void on_reset_defaults_clicked(GtkButton *button, LightSensorDialogData *data)
{
    (void)button;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_YES_NO,
                                              "Reset curve to default values?");
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_YES) {
        set_default_curve(data);
        refresh_curve_list(data);
    }
}

/* Save button clicked */
static void on_save_clicked(GtkButton *button, LightSensorDialogData *data)
{
    (void)button;

    if (data->point_count < 2) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                  GTK_DIALOG_MODAL,
                                                  GTK_MESSAGE_ERROR,
                                                  GTK_BUTTONS_OK,
                                                  "Please add at least 2 points to the curve.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    /* Save curve to configuration */
    config_save_light_sensor_curve(data->config, data->device_path, data->points, data->point_count);
    config_save(data->config);

    /* Close dialog */
    gtk_dialog_response(GTK_DIALOG(data->dialog), GTK_RESPONSE_OK);
}

/* Cancel button clicked */
static void on_cancel_clicked(GtkButton *button, LightSensorDialogData *data)
{
    (void)button;
    gtk_dialog_response(GTK_DIALOG(data->dialog), GTK_RESPONSE_CANCEL);
}

/* Lux value edited */
static void on_lux_edited(GtkCellRendererText *renderer, gchar *path, gchar *new_text, LightSensorDialogData *data)
{
    (void)renderer;

    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(data->list_store), &iter, path)) {
        return;
    }

    double old_lux;
    gtk_tree_model_get(GTK_TREE_MODEL(data->list_store), &iter, COL_LUX, &old_lux, -1);

    double new_lux = g_ascii_strtod(new_text, NULL);
    if (new_lux < 0) new_lux = 0;
    if (new_lux > 10000) new_lux = 10000;

    /* Check for duplicate */
    for (int i = 0; i < data->point_count; i++) {
        if (fabs(data->points[i].lux - new_lux) < 0.01 && fabs(data->points[i].lux - old_lux) > 0.01) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(data->dialog),
                                                      GTK_DIALOG_MODAL,
                                                      GTK_MESSAGE_WARNING,
                                                      GTK_BUTTONS_OK,
                                                      "Light level %.1f lux is already in the curve.",
                                                      new_lux);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            return;
        }
    }

    /* Update point */
    for (int i = 0; i < data->point_count; i++) {
        if (fabs(data->points[i].lux - old_lux) < 0.01) {
            data->points[i].lux = new_lux;
            break;
        }
    }

    /* Sort points by lux */
    qsort(data->points, data->point_count, sizeof(LightSensorCurvePoint), compare_lux);

    /* Refresh list and graph */
    refresh_curve_list(data);
}

/* Brightness value edited */
static void on_brightness_edited(GtkCellRendererText *renderer, gchar *path, gchar *new_text, LightSensorDialogData *data)
{
    (void)renderer;

    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(data->list_store), &iter, path)) {
        return;
    }

    double lux;
    gtk_tree_model_get(GTK_TREE_MODEL(data->list_store), &iter, COL_LUX, &lux, -1);

    int new_brightness = atoi(new_text);
    if (new_brightness < 0) new_brightness = 0;
    if (new_brightness > 100) new_brightness = 100;

    /* Update point */
    for (int i = 0; i < data->point_count; i++) {
        if (fabs(data->points[i].lux - lux) < 0.01) {
            data->points[i].brightness = new_brightness;
            break;
        }
    }

    /* Refresh list and graph */
    refresh_curve_list(data);
}

/* Linear interpolation helper */
static double interpolate_brightness(double lux, LightSensorCurvePoint *points, int count)
{
    if (count < 2) return 50.0;

    /* Below first point */
    if (lux <= points[0].lux) {
        return points[0].brightness;
    }

    /* Above last point */
    if (lux >= points[count - 1].lux) {
        return points[count - 1].brightness;
    }

    /* Find segment */
    for (int i = 0; i < count - 1; i++) {
        if (lux <= points[i + 1].lux) {
            double ratio = (lux - points[i].lux) / (points[i + 1].lux - points[i].lux);
            return points[i].brightness + ratio * (points[i + 1].brightness - points[i].brightness);
        }
    }

    return points[count - 1].brightness;
}

/* Draw the curve graph */
static gboolean on_graph_draw(GtkWidget *widget, cairo_t *cr, LightSensorDialogData *data)
{
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    const int margin = 40;
    const int graph_width = width - 2 * margin;
    const int graph_height = height - 2 * margin;

    /* Clear background */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    /* Draw axes */
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_set_line_width(cr, 1.0);

    /* X-axis */
    cairo_move_to(cr, margin, height - margin);
    cairo_line_to(cr, width - margin, height - margin);
    cairo_stroke(cr);

    /* Y-axis */
    cairo_move_to(cr, margin, margin);
    cairo_line_to(cr, margin, height - margin);
    cairo_stroke(cr);

    /* Draw grid lines */
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_set_line_width(cr, 0.5);

    /* Vertical grid lines (every 200 lux) */
    for (int lux = 0; lux <= 1000; lux += 200) {
        double x = margin + (lux / 1000.0) * graph_width;
        cairo_move_to(cr, x, margin);
        cairo_line_to(cr, x, height - margin);
        cairo_stroke(cr);
    }

    /* Horizontal grid lines (every 20%) */
    for (int brightness = 0; brightness <= 100; brightness += 20) {
        double y = height - margin - (brightness / 100.0) * graph_height;
        cairo_move_to(cr, margin, y);
        cairo_line_to(cr, width - margin, y);
        cairo_stroke(cr);
    }

    /* Draw axis labels */
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);

    /* X-axis labels */
    for (int lux = 0; lux <= 1000; lux += 200) {
        double x = margin + (lux / 1000.0) * graph_width;
        char label[16];
        snprintf(label, sizeof(label), "%d", lux);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, label, &extents);
        cairo_move_to(cr, x - extents.width / 2, height - margin + 15);
        cairo_show_text(cr, label);
    }

    /* Y-axis labels */
    for (int brightness = 0; brightness <= 100; brightness += 20) {
        double y = height - margin - (brightness / 100.0) * graph_height;
        char label[16];
        snprintf(label, sizeof(label), "%d%%", brightness);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, label, &extents);
        cairo_move_to(cr, margin - extents.width - 5, y + extents.height / 2);
        cairo_show_text(cr, label);
    }

    /* Axis titles */
    cairo_set_font_size(cr, 11);

    /* X-axis title */
    const char *x_title = "Ambient Light (lux)";
    cairo_text_extents_t extents;
    cairo_text_extents(cr, x_title, &extents);
    cairo_move_to(cr, (width - extents.width) / 2, height - 5);
    cairo_show_text(cr, x_title);

    /* Y-axis title (rotated) */
    const char *y_title = "Brightness (%)";
    cairo_save(cr);
    cairo_text_extents(cr, y_title, &extents);
    cairo_move_to(cr, 10, (height + extents.width) / 2);
    cairo_rotate(cr, -M_PI / 2);
    cairo_show_text(cr, y_title);
    cairo_restore(cr);

    /* Draw curve */
    if (data->point_count >= 2) {
        cairo_set_source_rgb(cr, 0.2, 0.4, 0.8);
        cairo_set_line_width(cr, 2.0);

        /* Find max lux for scaling */
        double max_lux = data->points[data->point_count - 1].lux;
        if (max_lux < 1000) max_lux = 1000;

        /* Draw curve with many segments for smooth interpolation */
        gboolean first = TRUE;
        for (int i = 0; i <= 100; i++) {
            double lux = (i / 100.0) * max_lux;
            double brightness = interpolate_brightness(lux, data->points, data->point_count);

            double x = margin + (lux / max_lux) * graph_width;
            double y = height - margin - (brightness / 100.0) * graph_height;

            if (first) {
                cairo_move_to(cr, x, y);
                first = FALSE;
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        cairo_stroke(cr);

        /* Draw points */
        cairo_set_source_rgb(cr, 0.8, 0.2, 0.2);
        for (int i = 0; i < data->point_count; i++) {
            double x = margin + (data->points[i].lux / max_lux) * graph_width;
            double y = height - margin - (data->points[i].brightness / 100.0) * graph_height;

            cairo_arc(cr, x, y, 4, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    return FALSE;
}

/* Redraw graph */
static void redraw_graph(LightSensorDialogData *data)
{
    gtk_widget_queue_draw(data->graph_drawing_area);
}
