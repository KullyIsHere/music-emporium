#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "gui.h"
#include "music.h"
#include "audio.h"
#include "export.h"

#define MAX_RECENT_FILES 12
#define DISCOVERY_DEPTH 3

enum {
    FILE_COL_NAME,
    FILE_COL_LOCATION,
    FILE_COL_PATH,
    FILE_N_COLUMNS
};

typedef enum {
    TOOL_SELECT,
    TOOL_BRUSH,
    TOOL_RECTANGLE,
    TOOL_DRAG
} EditorTool;

//Holds the loaded music data and current state for the GUI
typedef struct {
    MusicFile music[99];
    current_data current;
} AppState;

//Shared widgets and state used by the signal handlers
static GtkWidget *main_window;
static GtkWidget *statusbar;
static GtkWidget *main_stack;
static GtkWidget *editor_grid;
static GtkWidget *editor_title;
static GtkWidget *selection_label;
static GtkWidget *frequency_spin;
static GtkWidget *duration_spin;
static GtkWidget *play_button;
static GtkWidget *volume_label;
static GtkWidget *tool_buttons[4];
static GtkWidget *cell_buttons[100][10];
static guint status_ctx;
static AppState app_state;
static int selected_x = -1;
static int selected_y = -1;
static gboolean updating_controls = FALSE;
static guint playback_timer = 0;
static int playback_column = 0;
static int playhead_column = -1;
static gboolean playback_running = FALSE;
static int resize_start_columns = 0;
static int resize_start_rows = 0;
static int pending_columns = 0;
static int pending_rows = 0;
static int preview_columns = -1;
static int preview_rows = -1;
static GPtrArray *resize_preview_widgets = NULL;
static EditorTool active_tool = TOOL_SELECT;
static gboolean selected_tiles[100][10];
static gboolean changing_tool_buttons = FALSE;
static int gesture_start_x = -1;
static int gesture_start_y = -1;
static int brush_frequency = 440;
static int brush_duration = 250;
static void remember_recent_file(const char *path);
static void rebuild_editor(void);
static void clear_resize_preview(void);
static int snapped_drag_steps(double distance, int cell_size);

static const char *piano_names[10] = {
    "E5", "D♯5", "D5", "C♯5", "C5", "B4", "A♯4", "A4", "G♯4", "G4"
};

static const int piano_frequencies[10] = {
    659, 622, 587, 554, 523, 494, 466, 440, 415, 392
};

typedef struct {
    GtkWidget *dialog;
    GtkWidget *notebook;
    GtkWidget *recommended_view;
    GtkWidget *recent_view;
    GtkWidget *chooser;
    GtkWidget *open_button;
} OpenDialog;

//Shows a message in the status bar at the bottom of the window
static void show_status(const char *message) {
    gtk_statusbar_push(GTK_STATUSBAR(statusbar), status_ctx, message);
}

static void refresh_cell(int x, int y) {
    GtkWidget *button = cell_buttons[x][y];
    if (button == NULL)
        return;

    GtkStyleContext *context = gtk_widget_get_style_context(button);
    gtk_style_context_remove_class(context, "step-active");
    gtk_style_context_remove_class(context, "step-selected");
    gtk_style_context_remove_class(context, "tile-selected");
    if (app_state.music[0].music_data[x][y][0] > 0 &&
        app_state.music[0].music_data[x][y][1] > 0) {
        gtk_style_context_add_class(context, "step-active");
        gtk_button_set_label(GTK_BUTTON(button), "●");
    } else {
        gtk_button_set_label(GTK_BUTTON(button), "");
    }
    if (x == selected_x && y == selected_y)
        gtk_style_context_add_class(context, "step-selected");
    if (selected_tiles[x][y])
        gtk_style_context_add_class(context, "tile-selected");
}

static void clear_tile_selection(void) {
    for (int x = 0; x < app_state.music[0].columns; x++) {
        for (int y = 0; y < app_state.music[0].rows; y++) {
            selected_tiles[x][y] = FALSE;
            refresh_cell(x, y);
        }
    }
}

static void set_tile_selected(int x, int y, gboolean selected) {
    if (x < 0 || x >= app_state.music[0].columns || y < 0 || y >= app_state.music[0].rows)
        return;
    selected_tiles[x][y] = selected;
    refresh_cell(x, y);
}

static void paint_cell(int x, int y) {
    if (x < 0 || x >= app_state.music[0].columns || y < 0 || y >= app_state.music[0].rows)
        return;
    int frequency = brush_frequency > 0 ? brush_frequency : piano_frequencies[y];
    int duration = brush_duration > 0 ? brush_duration : 250;
    app_state.music[0].music_data[x][y][0] = frequency;
    app_state.music[0].music_data[x][y][1] = duration;
    int old_x = selected_x;
    int old_y = selected_y;
    selected_x = x;
    selected_y = y;
    if (old_x >= 0 && old_y >= 0) refresh_cell(old_x, old_y);
    refresh_cell(x, y);
}

static void select_cell(int x, int y) {
    int old_x = selected_x;
    int old_y = selected_y;
    selected_x = x;
    selected_y = y;

    if (old_x >= 0 && old_y >= 0)
        refresh_cell(old_x, old_y);
    refresh_cell(x, y);

    updating_controls = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(frequency_spin),
        app_state.music[0].music_data[x][y][0]);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_spin),
        app_state.music[0].music_data[x][y][1]);
    updating_controls = FALSE;

    char label[128];
    snprintf(label, sizeof(label), "%s  •  Step %d", piano_names[y], x + 1);
    gtk_label_set_text(GTK_LABEL(selection_label), label);
}

static void on_cell_clicked(GtkButton *button, gpointer user_data) {
    int packed = GPOINTER_TO_INT(user_data);
    int x = packed / 10;
    int y = packed % 10;
    if (active_tool == TOOL_BRUSH) {
        paint_cell(x, y);
    } else if (active_tool == TOOL_SELECT) {
        set_tile_selected(x, y, !selected_tiles[x][y]);
        select_cell(x, y);
    }
}

static void clear_drag_target_preview(void) {
    for (int x = 0; x < app_state.music[0].columns; x++) {
        for (int y = 0; y < app_state.music[0].rows; y++) {
            if (cell_buttons[x][y] != NULL)
                gtk_style_context_remove_class(
                    gtk_widget_get_style_context(cell_buttons[x][y]), "drag-target");
        }
    }
}

static void preview_drag_selection(int delta_x, int delta_y) {
    clear_drag_target_preview();
    for (int x = 0; x < app_state.music[0].columns; x++) {
        for (int y = 0; y < app_state.music[0].rows; y++) {
            if (!selected_tiles[x][y]) continue;
            int target_x = x + delta_x;
            int target_y = y + delta_y;
            if (target_x >= 0 && target_x < app_state.music[0].columns &&
                target_y >= 0 && target_y < app_state.music[0].rows) {
                gtk_style_context_add_class(
                    gtk_widget_get_style_context(cell_buttons[target_x][target_y]), "drag-target");
            }
        }
    }
}

static void move_selected_tiles(int delta_x, int delta_y) {
    int min_x = 100, max_x = -1, min_y = 10, max_y = -1;
    for (int x = 0; x < app_state.music[0].columns; x++) {
        for (int y = 0; y < app_state.music[0].rows; y++) {
            if (!selected_tiles[x][y]) continue;
            min_x = MIN(min_x, x); max_x = MAX(max_x, x);
            min_y = MIN(min_y, y); max_y = MAX(max_y, y);
        }
    }
    if (max_x < 0) return;
    delta_x = CLAMP(delta_x, -min_x, app_state.music[0].columns - 1 - max_x);
    delta_y = CLAMP(delta_y, -min_y, app_state.music[0].rows - 1 - max_y);
    if (delta_x == 0 && delta_y == 0) return;

    int notes[100][10][2];
    gboolean moved_selection[100][10] = {{FALSE}};
    memcpy(notes, app_state.music[0].music_data, sizeof(notes));
    for (int x = 0; x < app_state.music[0].columns; x++) {
        for (int y = 0; y < app_state.music[0].rows; y++) {
            if (selected_tiles[x][y]) {
                app_state.music[0].music_data[x][y][0] = 0;
                app_state.music[0].music_data[x][y][1] = 0;
            }
        }
    }
    for (int x = 0; x < app_state.music[0].columns; x++) {
        for (int y = 0; y < app_state.music[0].rows; y++) {
            if (!selected_tiles[x][y]) continue;
            int target_x = x + delta_x;
            int target_y = y + delta_y;
            app_state.music[0].music_data[target_x][target_y][0] = notes[x][y][0];
            app_state.music[0].music_data[target_x][target_y][1] = notes[x][y][1];
            moved_selection[target_x][target_y] = TRUE;
        }
    }
    memcpy(selected_tiles, moved_selection, sizeof(selected_tiles));
    selected_x = CLAMP(selected_x + delta_x, 0, app_state.music[0].columns - 1);
    selected_y = CLAMP(selected_y + delta_y, 0, app_state.music[0].rows - 1);
    for (int x = 0; x < app_state.music[0].columns; x++)
        for (int y = 0; y < app_state.music[0].rows; y++) refresh_cell(x, y);
    select_cell(selected_x, selected_y);
    show_status("Moved selected tiles. Save to keep the change.");
}

static void on_tile_drag_begin(GtkGestureDrag *gesture, double start_x,
                               double start_y, gpointer user_data) {
    int packed = GPOINTER_TO_INT(user_data);
    gesture_start_x = packed / 10;
    gesture_start_y = packed % 10;
    if (active_tool == TOOL_RECTANGLE) {
        clear_tile_selection();
        set_tile_selected(gesture_start_x, gesture_start_y, TRUE);
    } else if (active_tool == TOOL_DRAG && !selected_tiles[gesture_start_x][gesture_start_y]) {
        clear_tile_selection();
        set_tile_selected(gesture_start_x, gesture_start_y, TRUE);
        select_cell(gesture_start_x, gesture_start_y);
    } else if (active_tool == TOOL_BRUSH) {
        paint_cell(gesture_start_x, gesture_start_y);
    }
}

static void on_tile_drag_update(GtkGestureDrag *gesture, double offset_x,
                                double offset_y, gpointer user_data) {
    int target_x = CLAMP(gesture_start_x + snapped_drag_steps(offset_x, 46),
        0, app_state.music[0].columns - 1);
    int target_y = CLAMP(gesture_start_y + snapped_drag_steps(offset_y, 52),
        0, app_state.music[0].rows - 1);
    if (active_tool == TOOL_RECTANGLE) {
        clear_tile_selection();
        for (int x = MIN(gesture_start_x, target_x); x <= MAX(gesture_start_x, target_x); x++)
            for (int y = MIN(gesture_start_y, target_y); y <= MAX(gesture_start_y, target_y); y++)
                set_tile_selected(x, y, TRUE);
        select_cell(target_x, target_y);
    } else if (active_tool == TOOL_BRUSH) {
        int x = gesture_start_x;
        int y = gesture_start_y;
        int dx = ABS(target_x - x), sx = x < target_x ? 1 : -1;
        int dy = -ABS(target_y - y), sy = y < target_y ? 1 : -1;
        int error = dx + dy;
        while (TRUE) {
            paint_cell(x, y);
            if (x == target_x && y == target_y) break;
            int twice = 2 * error;
            if (twice >= dy) { error += dy; x += sx; }
            if (twice <= dx) { error += dx; y += sy; }
        }
    } else if (active_tool == TOOL_DRAG) {
        preview_drag_selection(target_x - gesture_start_x, target_y - gesture_start_y);
    }
}

static void on_tile_drag_end(GtkGestureDrag *gesture, double offset_x,
                             double offset_y, gpointer user_data) {
    if (active_tool == TOOL_DRAG) {
        int delta_x = snapped_drag_steps(offset_x, 46);
        int delta_y = snapped_drag_steps(offset_y, 52);
        clear_drag_target_preview();
        move_selected_tiles(delta_x, delta_y);
    }
    gesture_start_x = gesture_start_y = -1;
}

static void activate_editor_tool(EditorTool tool) {
    active_tool = tool;
    changing_tool_buttons = TRUE;
    for (int i = 0; i < 4; i++)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool_buttons[i]), i == tool);
    changing_tool_buttons = FALSE;
    clear_drag_target_preview();
    if (tool == TOOL_BRUSH) {
        int current_frequency = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(frequency_spin));
        int current_duration = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(duration_spin));
        if (current_frequency > 0) brush_frequency = current_frequency;
        if (current_duration > 0) brush_duration = current_duration;
        updating_controls = TRUE;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(frequency_spin), brush_frequency);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_spin), brush_duration);
        updating_controls = FALSE;
    }
    const char *names[] = {"Select", "Brush", "Rectangle", "Drag"};
    char message[80];
    g_snprintf(message, sizeof(message), "%s tool active.", names[tool]);
    show_status(message);
}

static void on_tool_toggled(GtkToggleButton *button, gpointer user_data) {
    if (changing_tool_buttons) return;
    EditorTool tool = (EditorTool)GPOINTER_TO_INT(user_data);
    if (gtk_toggle_button_get_active(button)) activate_editor_tool(tool);
    else if (active_tool == tool) gtk_toggle_button_set_active(button, TRUE);
}

static gboolean on_editor_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    if (gtk_stack_get_visible_child_name(GTK_STACK(main_stack)) == NULL ||
        strcmp(gtk_stack_get_visible_child_name(GTK_STACK(main_stack)), "editor") != 0)
        return FALSE;
    switch (gdk_keyval_to_lower(event->keyval)) {
        case GDK_KEY_s: activate_editor_tool(TOOL_SELECT); return TRUE;
        case GDK_KEY_b: activate_editor_tool(TOOL_BRUSH); return TRUE;
        case GDK_KEY_m: activate_editor_tool(TOOL_RECTANGLE); return TRUE;
        case GDK_KEY_d: activate_editor_tool(TOOL_DRAG); return TRUE;
        default: return FALSE;
    }
}

static void on_note_value_changed(GtkSpinButton *spin, gpointer user_data) {
    if (updating_controls || selected_x < 0 || selected_y < 0)
        return;

    int value_index = GPOINTER_TO_INT(user_data);
    if (active_tool == TOOL_BRUSH) {
        if (value_index == 0) brush_frequency = gtk_spin_button_get_value_as_int(spin);
        else brush_duration = gtk_spin_button_get_value_as_int(spin);
        return;
    }
    app_state.music[0].music_data[selected_x][selected_y][value_index] =
        gtk_spin_button_get_value_as_int(spin);
    refresh_cell(selected_x, selected_y);
}

static void on_use_piano_pitch(GtkButton *button, gpointer user_data) {
    int row = GPOINTER_TO_INT(user_data);
    if (selected_x < 0)
        select_cell(0, row);
    else
        select_cell(selected_x, row);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(frequency_spin), piano_frequencies[row]);
}

static void on_clear_cell(GtkButton *button, gpointer user_data) {
    if (selected_x < 0 || selected_y < 0)
        return;
    updating_controls = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(frequency_spin), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration_spin), 0);
    app_state.music[0].music_data[selected_x][selected_y][0] = 0;
    app_state.music[0].music_data[selected_x][selected_y][1] = 0;
    updating_controls = FALSE;
    refresh_cell(selected_x, selected_y);
}

static gboolean save_current_file(void) {
    const char *path = app_state.current.file;
    if (path[0] == '\0')
        return FALSE;

    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return FALSE;
    gboolean success = fwrite(&app_state.music[0], sizeof(MusicFile), 1, file) == 1;
    fclose(file);
    return success;
}

static char *path_with_extension(const char *path, const char *extension) {
    size_t path_length = strlen(path);
    size_t extension_length = strlen(extension);
    if (path_length >= extension_length &&
        g_ascii_strcasecmp(path + path_length - extension_length, extension) == 0)
        return g_strdup(path);
    return g_strconcat(path, extension, NULL);
}

static char *choose_export_path(const char *title, const char *suggested_name,
                                const char *filter_name, const char *pattern) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new(title, GTK_WINDOW(main_window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested_name);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, filter_name);
    gtk_file_filter_add_pattern(filter, pattern);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    char *path = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
    return path;
}

static void on_save_as_clicked(GtkWidget *widget, gpointer user_data) {
    char suggested[128];
    g_snprintf(suggested, sizeof(suggested), "%s.me", app_state.music[0].filename);
    char *chosen = choose_export_path("Save Composition As", suggested,
        "Music Emporium files (*.me)", "*.me");
    if (chosen == NULL) return;
    char *path = path_with_extension(chosen, ".me");
    g_free(chosen);

    char old_name[sizeof(app_state.music[0].filename)];
    g_strlcpy(old_name, app_state.music[0].filename, sizeof(old_name));
    char *basename = g_path_get_basename(path);
    char *dot = strrchr(basename, '.');
    if (dot != NULL) *dot = '\0';
    g_strlcpy(app_state.music[0].filename, basename, sizeof(app_state.music[0].filename));
    g_free(basename);

    FILE *file = g_fopen(path, "wb");
    gboolean success = file != NULL &&
        fwrite(&app_state.music[0], sizeof(MusicFile), 1, file) == 1;
    if (file != NULL) fclose(file);
    if (success) {
        g_strlcpy(app_state.current.file, path, sizeof(app_state.current.file));
        remember_recent_file(path);
        rebuild_editor();
        show_status("Saved a new .me copy and switched the editor to it.");
    } else {
        g_strlcpy(app_state.music[0].filename, old_name, sizeof(app_state.music[0].filename));
        show_status("Could not save the new .me file.");
    }
    g_free(path);
}

static void on_export_clicked(GtkWidget *widget, gpointer user_data) {
    gboolean mp4 = GPOINTER_TO_INT(user_data);
    const char *extension = mp4 ? ".mp4" : ".wav";
    char suggested[128];
    g_snprintf(suggested, sizeof(suggested), "%s%s",
        app_state.music[0].filename, extension);
    char *chosen = choose_export_path(mp4 ? "Export MP4" : "Export WAV", suggested,
        mp4 ? "MP4 video (*.mp4)" : "Wave audio (*.wav)", mp4 ? "*.mp4" : "*.wav");
    if (chosen == NULL) return;
    char *path = path_with_extension(chosen, extension);
    g_free(chosen);

    show_status(mp4 ? "Rendering MP4…" : "Rendering WAV…");
    while (gtk_events_pending()) gtk_main_iteration();
    char error[256] = "";
    int success = mp4
        ? export_music_mp4(&app_state.music[0], path, audio_get_volume(), error, sizeof(error))
        : export_music_wav(&app_state.music[0], path, audio_get_volume(), error, sizeof(error));
    if (success) {
        char message[640];
        g_snprintf(message, sizeof(message), "Exported %s", path);
        show_status(message);
    } else {
        show_status(error);
    }
    g_free(path);
}

static void on_save_clicked(GtkButton *button, gpointer user_data) {
    if (save_current_file()) {
        show_status("Saved your composition.");
        remember_recent_file(app_state.current.file);
    } else {
        show_status("Could not save this file.");
    }
}

static void on_back_to_home(GtkButton *button, gpointer user_data) {
    if (playback_timer != 0) {
        g_source_remove(playback_timer);
        playback_timer = 0;
    }
    playback_running = FALSE;
    audio_stop_all();
    if (play_button != NULL)
        gtk_button_set_label(GTK_BUTTON(play_button), "▶  Play");
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "home");
    gtk_window_set_title(GTK_WINDOW(main_window), "Music Emporium");
}

static void show_playhead(int column) {
    int rows = app_state.music[0].rows;
    if (playhead_column >= 0) {
        for (int y = 0; y < rows; y++) {
            if (cell_buttons[playhead_column][y] != NULL)
                gtk_style_context_remove_class(
                    gtk_widget_get_style_context(cell_buttons[playhead_column][y]), "step-playing");
        }
    }
    playhead_column = column;
    if (column >= 0) {
        for (int y = 0; y < rows; y++) {
            if (cell_buttons[column][y] != NULL)
                gtk_style_context_add_class(
                    gtk_widget_get_style_context(cell_buttons[column][y]), "step-playing");
        }
    }
}

static int play_column(int column) {
    MusicFile *music = &app_state.music[0];
    int longest_duration = 0;
    show_playhead(column);
    for (int y = 0; y < music->rows; y++) {
        int frequency = music->music_data[column][y][0];
        int duration = music->music_data[column][y][1];
        if (frequency > 0 && duration > 0) {
            play_tone(frequency, duration);
            if (duration > longest_duration)
                longest_duration = duration;
        }
    }
    return MAX(40, longest_duration + music->Intervals[column]);
}

static void pause_playback(void) {
    playback_running = FALSE;
    audio_stop_all();
    if (playback_timer != 0) {
        g_source_remove(playback_timer);
        playback_timer = 0;
    }
    if (play_button != NULL)
        gtk_button_set_label(GTK_BUTTON(play_button), "▶  Play");
}

static gboolean advance_playback(gpointer user_data);

static void play_current_column(void) {
    int delay = play_column(playback_column);
    playback_timer = g_timeout_add(delay, advance_playback, NULL);
}

static gboolean advance_playback(gpointer user_data) {
    playback_timer = 0;
    if (!playback_running)
        return G_SOURCE_REMOVE;

    playback_column++;
    if (playback_column >= app_state.music[0].columns) {
        playback_column = app_state.music[0].columns - 1;
        pause_playback();
        show_status("Playback finished.");
        return G_SOURCE_REMOVE;
    }

    play_current_column();
    return G_SOURCE_REMOVE;
}

static void on_play_pause_clicked(GtkButton *button, gpointer user_data) {
    if (playback_running) {
        pause_playback();
        show_status("Playback paused.");
        return;
    }

    if (playback_column >= app_state.music[0].columns - 1)
        playback_column = 0;
    playback_running = TRUE;
    gtk_button_set_label(GTK_BUTTON(play_button), "Ⅱ  Pause");
    show_status("Playing composition…");
    play_current_column();
}

static void move_playhead(int direction) {
    pause_playback();
    playback_column += direction;
    if (playback_column < 0)
        playback_column = 0;
    if (playback_column >= app_state.music[0].columns)
        playback_column = app_state.music[0].columns - 1;
    show_playhead(playback_column);
    if (selected_y >= 0)
        select_cell(playback_column, selected_y);

    char message[80];
    snprintf(message, sizeof(message), "Moved to step %d.", playback_column + 1);
    show_status(message);
}

static void on_previous_clicked(GtkButton *button, gpointer user_data) {
    move_playhead(-1);
}

static void on_next_clicked(GtkButton *button, gpointer user_data) {
    move_playhead(1);
}

static void on_volume_changed(GtkRange *range, gpointer user_data) {
    int percent = (int)gtk_range_get_value(range);
    audio_set_volume(percent / 100.0);

    char text[24];
    if (percent == 0)
        snprintf(text, sizeof(text), "MUTE");
    else
        snprintf(text, sizeof(text), "%d%%", percent);
    gtk_label_set_text(GTK_LABEL(volume_label), text);
}

static int snapped_drag_steps(double distance, int cell_size) {
    if (distance >= 0)
        return (int)((distance + cell_size / 2.0) / cell_size);
    return (int)((distance - cell_size / 2.0) / cell_size);
}

static void on_resize_drag_begin(GtkGestureDrag *gesture, double start_x,
                                 double start_y, gpointer user_data) {
    clear_resize_preview();
    resize_start_columns = app_state.music[0].columns;
    resize_start_rows = app_state.music[0].rows;
    preview_columns = resize_start_columns;
    preview_rows = resize_start_rows;
}

static void clear_resize_preview(void) {
    if (resize_preview_widgets != NULL) {
        for (guint i = 0; i < resize_preview_widgets->len; i++) {
            GtkWidget *widget = g_ptr_array_index(resize_preview_widgets, i);
            if (GTK_IS_WIDGET(widget)) gtk_widget_destroy(widget);
        }
        g_ptr_array_set_size(resize_preview_widgets, 0);
    }
    for (int x = 0; x < app_state.music[0].columns; x++) {
        for (int y = 0; y < app_state.music[0].rows; y++) {
            if (cell_buttons[x][y] != NULL) {
                gtk_style_context_remove_class(
                    gtk_widget_get_style_context(cell_buttons[x][y]), "resize-remove-preview");
                gtk_widget_set_opacity(cell_buttons[x][y], 1.0);
            }
        }
    }
}

static GtkWidget *add_resize_preview_cell(int column, int row, int width, int height) {
    GtkWidget *cell = gtk_label_new("");
    gtk_widget_set_size_request(cell, width, height);
    gtk_widget_set_opacity(cell, 0.48);
    gtk_style_context_add_class(gtk_widget_get_style_context(cell), "resize-add-preview");
    gtk_grid_attach(GTK_GRID(editor_grid), cell, column, row, 1, 1);
    g_ptr_array_add(resize_preview_widgets, cell);
    return cell;
}

static void update_resize_preview(int target_columns, int target_rows) {
    MusicFile *music = &app_state.music[0];
    target_columns = CLAMP(target_columns, 10, 100);
    target_rows = CLAMP(target_rows, 1, 10);
    if (target_columns == preview_columns && target_rows == preview_rows)
        return;

    clear_resize_preview();
    preview_columns = target_columns;
    preview_rows = target_rows;
    if (resize_preview_widgets == NULL)
        resize_preview_widgets = g_ptr_array_new();

    if (target_columns > music->columns) {
        for (int x = music->columns; x < target_columns; x++) {
            int grid_x = x + 2; /* Preview is drawn just beyond the live edge grip. */
            GtkWidget *number = add_resize_preview_cell(grid_x, 0, 42, 18);
            char text[12];
            g_snprintf(text, sizeof(text), "%d", x + 1);
            gtk_label_set_text(GTK_LABEL(number), text);
            for (int y = 0; y < music->rows; y++)
                add_resize_preview_cell(grid_x, y + 1, 42, 48);
        }
    } else if (target_columns < music->columns) {
        for (int x = target_columns; x < music->columns; x++) {
            for (int y = 0; y < music->rows; y++) {
                gtk_style_context_add_class(
                    gtk_widget_get_style_context(cell_buttons[x][y]), "resize-remove-preview");
                gtk_widget_set_opacity(cell_buttons[x][y], 0.38);
            }
        }
    }

    if (target_rows > music->rows) {
        for (int y = music->rows; y < target_rows; y++) {
            int grid_y = y + 2; /* Preview is drawn below the live edge grip. */
            add_resize_preview_cell(0, grid_y, 82, 48);
            for (int x = 0; x < music->columns; x++)
                add_resize_preview_cell(x + 1, grid_y, 42, 48);
        }
    } else if (target_rows < music->rows) {
        for (int y = target_rows; y < music->rows; y++) {
            for (int x = 0; x < music->columns; x++) {
                gtk_style_context_add_class(
                    gtk_widget_get_style_context(cell_buttons[x][y]), "resize-remove-preview");
                gtk_widget_set_opacity(cell_buttons[x][y], 0.38);
            }
        }
    }
    gtk_widget_show_all(editor_grid);
}

static void on_resize_drag_update(GtkGestureDrag *gesture, double offset_x,
                                  double offset_y, gpointer user_data) {
    int axis = GPOINTER_TO_INT(user_data);
    int columns = resize_start_columns;
    int rows = resize_start_rows;
    if (axis == 0)
        columns = CLAMP(resize_start_columns + snapped_drag_steps(offset_x, 46), 10, 100);
    else
        rows = CLAMP(resize_start_rows + snapped_drag_steps(offset_y, 52), 1, 10);
    update_resize_preview(columns, rows);
}

static void resize_music_grid(int new_columns, int new_rows) {
    MusicFile *music = &app_state.music[0];
    int old_columns = music->columns;
    int old_rows = music->rows;

    new_columns = CLAMP(new_columns, 10, 100);
    new_rows = CLAMP(new_rows, 1, 10);
    if (new_columns == old_columns && new_rows == old_rows)
        return;

    if (new_columns > old_columns) {
        for (int x = old_columns; x < new_columns; x++) {
            memset(music->music_data[x], 0, sizeof(music->music_data[x]));
            music->Intervals[x] = 400;
        }
    }
    if (new_rows > old_rows) {
        for (int x = 0; x < new_columns; x++) {
            for (int y = old_rows; y < new_rows; y++)
                memset(music->music_data[x][y], 0, sizeof(music->music_data[x][y]));
        }
    }

    music->columns = new_columns;
    music->rows = new_rows;
    rebuild_editor();

    char message[96];
    snprintf(message, sizeof(message), "Grid resized to %d steps × %d notes. Save to keep it.",
        music->columns, music->rows);
    show_status(message);
}

static gboolean apply_pending_resize(gpointer user_data) {
    resize_music_grid(pending_columns, pending_rows);
    return G_SOURCE_REMOVE;
}

static void on_grid_size_clicked(GtkWidget *widget, gpointer user_data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Grid Size", GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    GtkWidget *column_label = gtk_label_new("Columns / steps");
    GtkWidget *row_label = gtk_label_new("Rows / notes");
    GtkWidget *column_spin = gtk_spin_button_new_with_range(10, 100, 1);
    GtkWidget *row_spin = gtk_spin_button_new_with_range(1, 10, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(column_spin), app_state.music[0].columns);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(row_spin), app_state.music[0].rows);
    gtk_grid_attach(GTK_GRID(grid), column_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), column_spin, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), row_spin, 1, 1, 1, 1);
    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        resize_music_grid(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(column_spin)),
            gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(row_spin)));
    }
    gtk_widget_destroy(dialog);
}

static void on_adjust_grid_clicked(GtkWidget *widget, gpointer user_data) {
    int action = GPOINTER_TO_INT(user_data);
    int columns = app_state.music[0].columns;
    int rows = app_state.music[0].rows;
    if (action == 0) columns++;
    else if (action == 1) columns--;
    else if (action == 2) rows++;
    else rows--;
    resize_music_grid(columns, rows);
}

static void on_resize_drag_end(GtkGestureDrag *gesture, double offset_x,
                               double offset_y, gpointer user_data) {
    int axis = GPOINTER_TO_INT(user_data);
    pending_columns = resize_start_columns;
    pending_rows = resize_start_rows;

    if (axis == 0) {
        pending_columns = CLAMP(resize_start_columns + snapped_drag_steps(offset_x, 46), 10, 100);
    } else {
        pending_rows = CLAMP(resize_start_rows + snapped_drag_steps(offset_y, 52), 1, 10);
    }

    clear_resize_preview();
    preview_columns = -1;
    preview_rows = -1;
    if (pending_columns != app_state.music[0].columns ||
        pending_rows != app_state.music[0].rows) {
        g_idle_add(apply_pending_resize, NULL);
    }
}

static gboolean on_resize_grip_enter(GtkWidget *widget, GdkEventCrossing *event,
                                     gpointer user_data) {
    int axis = GPOINTER_TO_INT(user_data);
    GdkDisplay *display = gtk_widget_get_display(widget);
    GdkCursor *cursor = gdk_cursor_new_for_display(display,
        axis == 0 ? GDK_SB_H_DOUBLE_ARROW : GDK_SB_V_DOUBLE_ARROW);
    gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);
    g_object_unref(cursor);
    return FALSE;
}

static gboolean on_resize_grip_leave(GtkWidget *widget, GdkEventCrossing *event,
                                     gpointer user_data) {
    gdk_window_set_cursor(gtk_widget_get_window(widget), NULL);
    return FALSE;
}

static GtkWidget *make_resize_grip(int axis) {
    GtkWidget *grip = gtk_event_box_new();
    GtkWidget *text = gtk_label_new(" ");
    gtk_container_add(GTK_CONTAINER(grip), text);
    gtk_style_context_add_class(gtk_widget_get_style_context(grip), "resize-grip");
    gtk_widget_add_events(grip, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(grip, "enter-notify-event", G_CALLBACK(on_resize_grip_enter), GINT_TO_POINTER(axis));
    g_signal_connect(grip, "leave-notify-event", G_CALLBACK(on_resize_grip_leave), GINT_TO_POINTER(axis));

    GtkGesture *drag = gtk_gesture_drag_new(grip);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_resize_drag_begin), GINT_TO_POINTER(axis));
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_resize_drag_update), GINT_TO_POINTER(axis));
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_resize_drag_end), GINT_TO_POINTER(axis));
    g_object_set_data_full(G_OBJECT(grip), "resize-gesture", drag, g_object_unref);
    return grip;
}

static void rebuild_editor(void) {
    pause_playback();
    show_playhead(-1);
    clear_resize_preview();
    GList *children = gtk_container_get_children(GTK_CONTAINER(editor_grid));
    for (GList *item = children; item != NULL; item = item->next)
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
    memset(cell_buttons, 0, sizeof(cell_buttons));

    selected_x = -1;
    selected_y = -1;
    playback_column = 0;
    playhead_column = -1;
    MusicFile *music = &app_state.music[0];
    char heading[160];
    snprintf(heading, sizeof(heading), "%s  •  %d steps × %d notes",
        music->filename, music->columns, music->rows);
    gtk_label_set_text(GTK_LABEL(editor_title), heading);

    for (int x = 0; x < music->columns; x++) {
        char number[12];
        snprintf(number, sizeof(number), "%d", x + 1);
        GtkWidget *label = gtk_label_new(number);
        gtk_style_context_add_class(gtk_widget_get_style_context(label), "step-number");
        gtk_grid_attach(GTK_GRID(editor_grid), label, x + 1, 0, 1, 1);
    }

    for (int y = 0; y < music->rows; y++) {
        char note[32];
        snprintf(note, sizeof(note), "%s\n%d Hz", piano_names[y], piano_frequencies[y]);
        GtkWidget *key = gtk_button_new_with_label(note);
        gtk_widget_set_size_request(key, 82, 48);
        gtk_style_context_add_class(gtk_widget_get_style_context(key),
            (y == 1 || y == 3 || y == 6 || y == 8) ? "piano-black" : "piano-white");
        g_signal_connect(key, "clicked", G_CALLBACK(on_use_piano_pitch), GINT_TO_POINTER(y));
        gtk_grid_attach(GTK_GRID(editor_grid), key, 0, y + 1, 1, 1);

        for (int x = 0; x < music->columns; x++) {
            GtkWidget *cell = gtk_button_new();
            gtk_widget_set_size_request(cell, 42, 48);
            gtk_style_context_add_class(gtk_widget_get_style_context(cell), "step-cell");
            g_signal_connect(cell, "clicked", G_CALLBACK(on_cell_clicked),
                GINT_TO_POINTER(x * 10 + y));
            GtkGesture *tile_drag = gtk_gesture_drag_new(cell);
            gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tile_drag), GDK_BUTTON_PRIMARY);
            g_signal_connect(tile_drag, "drag-begin", G_CALLBACK(on_tile_drag_begin),
                GINT_TO_POINTER(x * 10 + y));
            g_signal_connect(tile_drag, "drag-update", G_CALLBACK(on_tile_drag_update),
                GINT_TO_POINTER(x * 10 + y));
            g_signal_connect(tile_drag, "drag-end", G_CALLBACK(on_tile_drag_end),
                GINT_TO_POINTER(x * 10 + y));
            g_object_set_data_full(G_OBJECT(cell), "tile-drag", tile_drag, g_object_unref);
            gtk_grid_attach(GTK_GRID(editor_grid), cell, x + 1, y + 1, 1, 1);
            cell_buttons[x][y] = cell;
            refresh_cell(x, y);
        }
    }

    GtkWidget *column_grip = make_resize_grip(0);
    gtk_widget_set_size_request(column_grip, 12, -1);
    gtk_grid_attach(GTK_GRID(editor_grid), column_grip,
        music->columns + 1, 1, 1, music->rows);

    GtkWidget *row_grip = make_resize_grip(1);
    gtk_widget_set_size_request(row_grip, -1, 12);
    gtk_grid_attach(GTK_GRID(editor_grid), row_grip,
        1, music->rows + 1, music->columns, 1);

    gtk_widget_show_all(editor_grid);
    if (music->columns > 0 && music->rows > 0)
        select_cell(0, 0);
}

static void show_editor(void) {
    memset(selected_tiles, 0, sizeof(selected_tiles));
    rebuild_editor();
    activate_editor_tool(TOOL_SELECT);
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "editor");
    gtk_window_set_title(GTK_WINDOW(main_window), "Music Emporium — Studio");
}

static char *recent_file_path(void) {
    return g_build_filename(g_get_user_config_dir(), "music-emporium", "recent-files", NULL);
}

static GPtrArray *load_recent_files(void) {
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    char *filename = recent_file_path();
    char *contents = NULL;
    gsize length = 0;

    if (g_file_get_contents(filename, &contents, &length, NULL)) {
        char **lines = g_strsplit(contents, "\n", MAX_RECENT_FILES + 1);
        for (int i = 0; lines[i] != NULL && paths->len < MAX_RECENT_FILES; i++) {
            if (lines[i][0] != '\0' && g_file_test(lines[i], G_FILE_TEST_IS_REGULAR))
                g_ptr_array_add(paths, g_strdup(lines[i]));
        }
        g_strfreev(lines);
    }

    g_free(contents);
    g_free(filename);
    return paths;
}

static void remember_recent_file(const char *path) {
    GPtrArray *old_paths = load_recent_files();
    GString *contents = g_string_new(path);
    int kept = 1;

    g_string_append_c(contents, '\n');
    for (guint i = 0; i < old_paths->len && kept < MAX_RECENT_FILES; i++) {
        const char *old_path = g_ptr_array_index(old_paths, i);
        if (g_strcmp0(path, old_path) != 0) {
            g_string_append_printf(contents, "%s\n", old_path);
            kept++;
        }
    }

    char *filename = recent_file_path();
    char *directory = g_path_get_dirname(filename);
    if (g_mkdir_with_parents(directory, 0700) == 0)
        g_file_set_contents(filename, contents->str, contents->len, NULL);

    g_free(directory);
    g_free(filename);
    g_string_free(contents, TRUE);
    g_ptr_array_free(old_paths, TRUE);
}

static gboolean has_me_extension(const char *name) {
    const char *extension = strrchr(name, '.');
    return extension != NULL && g_ascii_strcasecmp(extension, ".me") == 0;
}

static void add_path_to_store(GtkListStore *store, const char *path) {
    GtkTreeIter iter;
    char *basename = g_path_get_basename(path);
    char *directory = g_path_get_dirname(path);
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
        FILE_COL_NAME, basename,
        FILE_COL_LOCATION, directory,
        FILE_COL_PATH, path,
        -1);
    g_free(directory);
    g_free(basename);
}

static void discover_me_files(GtkListStore *store, const char *directory,
                              int depth, GHashTable *seen) {
    if (directory == NULL || depth < 0 || !g_file_test(directory, G_FILE_TEST_IS_DIR))
        return;

    GDir *dir = g_dir_open(directory, 0, NULL);
    if (dir == NULL)
        return;

    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (name[0] == '.')
            continue;

        char *path = g_build_filename(directory, name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR) && has_me_extension(name)) {
            char *canonical = g_canonicalize_filename(path, NULL);
            if (g_hash_table_add(seen, canonical))
                add_path_to_store(store, path);
            else
                g_free(canonical);
        } else if (depth > 0 && g_file_test(path, G_FILE_TEST_IS_DIR)) {
            discover_me_files(store, path, depth - 1, seen);
        }
        g_free(path);
    }
    g_dir_close(dir);
}

static GtkWidget *make_file_view(GtkListStore *store) {
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),
        gtk_tree_view_column_new_with_attributes("File", renderer, "text", FILE_COL_NAME, NULL));
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),
        gtk_tree_view_column_new_with_attributes("Location", renderer, "text", FILE_COL_LOCATION, NULL));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), TRUE);
    return view;
}

static char *selected_view_path(GtkWidget *view) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    GtkTreeModel *model;
    GtkTreeIter iter;
    char *path = NULL;
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, FILE_COL_PATH, &path, -1);
    return path;
}

static char *open_dialog_selected_path(OpenDialog *state) {
    int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(state->notebook));
    if (page == 0)
        return selected_view_path(state->recommended_view);
    if (page == 1)
        return selected_view_path(state->recent_view);
    return gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(state->chooser));
}

static void update_open_button(OpenDialog *state) {
    char *path = open_dialog_selected_path(state);
    gtk_widget_set_sensitive(state->open_button,
        path != NULL && has_me_extension(path) && g_file_test(path, G_FILE_TEST_IS_REGULAR));
    g_free(path);
}

static void on_open_selection_changed(gpointer source, gpointer user_data) {
    update_open_button((OpenDialog *)user_data);
}

static void on_open_page_changed(GtkNotebook *notebook, GtkWidget *page,
                                 guint page_num, gpointer user_data) {
    update_open_button((OpenDialog *)user_data);
}

static void on_file_row_activated(GtkTreeView *view, GtkTreePath *path,
                                  GtkTreeViewColumn *column, gpointer user_data) {
    OpenDialog *state = user_data;
    update_open_button(state);
    if (gtk_widget_get_sensitive(state->open_button))
        gtk_dialog_response(GTK_DIALOG(state->dialog), GTK_RESPONSE_ACCEPT);
}

static GtkWidget *scroll_view(GtkWidget *view) {
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    return scroll;
}

//Quits the application
static void on_quit_clicked(GtkButton *button, gpointer data) {
    gtk_main_quit();
}

//New file button - asks for a name, columns and rows, then saves the file
static void on_new_clicked(GtkButton *button, gpointer data) {
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "New File");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(main_window));
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Create", GTK_RESPONSE_OK);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

    GtkWidget *lbl_name = gtk_label_new("File name:");
    GtkWidget *entry_name = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry_name), TRUE);

    GtkWidget *lbl_columns = gtk_label_new("Columns (10-100):");
    GtkWidget *entry_columns = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry_columns), TRUE);

    GtkWidget *lbl_rows = gtk_label_new("Rows (1-10):");
    GtkWidget *entry_rows = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry_rows), TRUE);

    gtk_grid_attach(GTK_GRID(grid), lbl_name, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_name, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_columns, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_columns, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lbl_rows, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_rows, 1, 2, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry_name));
        int columns = atoi(gtk_entry_get_text(GTK_ENTRY(entry_columns)));
        int rows = atoi(gtk_entry_get_text(GTK_ENTRY(entry_rows)));

        if (name == NULL || name[0] == '\0') {
            show_status("Please enter a file name.");
        }
        else if (make_music_file(&app_state.music[0], name, columns, rows) != 0) {
            show_status("Columns must be 10-100 and rows must be 1-10.");
        }
        else {
            save_music_file(&app_state.music[0]);

            char *saved_name = g_strdup_printf("%s.me", name);
            char *saved_path = g_canonicalize_filename(saved_name, NULL);
            remember_recent_file(saved_path);

            app_state.current.file_index = 0;
            g_strlcpy(app_state.current.file, saved_path, sizeof(app_state.current.file));
            g_strlcpy(app_state.current.file_list[0], saved_path,
                sizeof(app_state.current.file_list[0]));
            app_state.current.file_sum = 1;

            char msg[512];
            snprintf(msg, sizeof(msg), "Created %s.me (%d rows, %d columns).",
                name, rows, columns);
            show_status(msg);

            show_editor();
            g_free(saved_path);
            g_free(saved_name);
        }
    }

    gtk_widget_destroy(dialog);
}

//Open file button - lets the user pick a .me file and loads it
static void on_open_clicked(GtkButton *button, gpointer data) {
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Open Music Emporium File");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 820, 540);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(main_window));
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Open", GTK_RESPONSE_ACCEPT);

    OpenDialog state = {0};
    state.dialog = dialog;
    state.open_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_widget_set_sensitive(state.open_button, FALSE);

    GtkWidget *notebook = gtk_notebook_new();
    state.notebook = notebook;
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), notebook, TRUE, TRUE, 0);

    GtkListStore *recommended_store = gtk_list_store_new(FILE_N_COLUMNS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const char *home = g_get_home_dir();
    const char *special_folders[] = {
        g_get_user_special_dir(G_USER_DIRECTORY_MUSIC),
        g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS),
        g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD),
        g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP),
        g_get_current_dir()
    };
    for (guint i = 0; i < G_N_ELEMENTS(special_folders); i++)
        discover_me_files(recommended_store, special_folders[i],
            g_strcmp0(special_folders[i], home) == 0 ? 1 : DISCOVERY_DEPTH, seen);

    GVolumeMonitor *monitor = g_volume_monitor_get();
    GList *mounts = g_volume_monitor_get_mounts(monitor);
    for (GList *item = mounts; item != NULL; item = item->next) {
        GFile *root = g_mount_get_root(G_MOUNT(item->data));
        char *path = g_file_get_path(root);
        /* Keep automatic discovery quick: inspect each drive root and its
           immediate folders. The Browse tab remains available for deep paths. */
        discover_me_files(recommended_store, path, 1, seen);
        g_free(path);
        g_object_unref(root);
    }
    g_list_free_full(mounts, g_object_unref);
    g_object_unref(monitor);
    g_hash_table_destroy(seen);
    g_free((gpointer)special_folders[G_N_ELEMENTS(special_folders) - 1]);

    state.recommended_view = make_file_view(recommended_store);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll_view(state.recommended_view),
        gtk_label_new("Recommended"));
    g_object_unref(recommended_store);

    GtkListStore *recent_store = gtk_list_store_new(FILE_N_COLUMNS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GPtrArray *recent_paths = load_recent_files();
    for (guint i = 0; i < recent_paths->len; i++)
        add_path_to_store(recent_store, g_ptr_array_index(recent_paths, i));
    g_ptr_array_free(recent_paths, TRUE);
    state.recent_view = make_file_view(recent_store);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll_view(state.recent_view),
        gtk_label_new("Recent"));
    g_object_unref(recent_store);

    GtkWidget *chooser = gtk_file_chooser_widget_new(GTK_FILE_CHOOSER_ACTION_OPEN);
    state.chooser = chooser;
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), chooser, gtk_label_new("Browse drives"));

    //Only show .me files, with an all-files option as a fallback
    GtkFileFilter *me_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(me_filter, "Music Emporium files (*.me)");
    gtk_file_filter_add_pattern(me_filter, "*.me");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), me_filter);

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All files");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all_filter);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), me_filter);

    g_signal_connect(state.recommended_view, "cursor-changed", G_CALLBACK(on_open_selection_changed), &state);
    g_signal_connect(state.recommended_view, "row-activated", G_CALLBACK(on_file_row_activated), &state);
    g_signal_connect(state.recent_view, "cursor-changed", G_CALLBACK(on_open_selection_changed), &state);
    g_signal_connect(state.recent_view, "row-activated", G_CALLBACK(on_file_row_activated), &state);
    g_signal_connect(chooser, "selection-changed", G_CALLBACK(on_open_selection_changed), &state);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(on_open_page_changed), &state);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = open_dialog_selected_path(&state);
        if (filename != NULL) {
            //Loads it into the first slot and reports what was opened
            app_state.current.file_sum = 1;
            strcpy(app_state.current.file_list[0], filename);

            if (load_music_file_path(&app_state.music[0], filename)) {
                app_state.current.file_index = 0;
                strcpy(app_state.current.file, filename);

                char msg[512];
                snprintf(msg, sizeof(msg), "Opened %s (%d rows, %d columns).",
                    filename, app_state.music[0].rows, app_state.music[0].columns);
                show_status(msg);

                gtk_window_set_title(GTK_WINDOW(main_window), "Music Emporium - file open");
                remember_recent_file(filename);
                show_editor();
            } else {
                show_status("That file is not a valid Music Emporium composition.");
            }

            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
}

//Builds the main menu window and returns it.
GtkWidget *build_main_window(void) {
    const char *css =
        "* { font-family: 'Consolas', 'Courier New', monospace; }"
        "window { color: #eeeaf4; background-image: linear-gradient(to bottom, #241a35 0%, #241a35 14%, #302044 14%, #302044 28%, #3d2755 28%, #3d2755 42%, #4a2d67 42%, #4a2d67 56%, #573478 56%, #573478 70%, #633b88 70%, #633b88 84%, #71479a 84%, #71479a 100%); }"
        ".home-panel, .editor-panel, .inspector { background: #292532; color: #eeeaf4; border-radius: 2px; border: 2px solid #17141d; padding: 18px; box-shadow: 4px 4px #151119; }"
        ".home-panel { border-top: 5px solid #a875db; }"
        ".editor-panel { background: #211e28; }"
        ".inspector { background: #302b39; border-top: 5px solid #7465a8; }"
        ".retro-toolbar { background-image: linear-gradient(to bottom, #8f72be 0%, #8f72be 18%, #67508e 18%, #67508e 52%, #493760 52%, #493760 100%); border: 2px solid #1a1422; border-radius: 3px; padding: 7px; box-shadow: 3px 3px #17121d; }"
        ".retro-menu { background: #211d27; color: #eeeaf4; border: 2px solid #17131c; padding: 2px; }"
        ".retro-menu menuitem { padding: 5px 12px; } .retro-menu menuitem:hover { background: #765092; color: white; }"
        ".transport { background: #28232f; border: 1px solid #16121b; padding: 3px; }"
        ".tool-button { background: #393241; color: #d8cedf; border-radius: 1px; border: 1px solid #17131c; padding: 7px 9px; }"
        ".tool-button:checked { background-image: linear-gradient(to bottom, #d0a3ef, #76469a); color: white; border: 2px solid #f2ddff; }"
        ".hero-title { color: #ffffff; font-size: 38px; font-weight: 800; background: #51346f; border: 3px solid #d2afe9; padding: 10px 24px; box-shadow: 5px 5px #21152d; }"
        ".hero-subtitle { color: #f4e9ff; font-size: 15px; font-weight: 700; }"
        ".section-title { color: #cf9ff1; font-size: 20px; font-weight: 700; }"
        ".muted { color: #b9afc4; }"
        ".primary { background-image: linear-gradient(to bottom, #c39ae7 0%, #c39ae7 18%, #8a55bb 18%, #8a55bb 55%, #66368f 55%, #66368f 100%); color: white; border-radius: 2px; border: 2px solid #2e193f; padding: 8px 16px; font-weight: 700; }"
        ".primary:hover { background-image: linear-gradient(to bottom, #dec1f6 0%, #dec1f6 22%, #a96bdc 22%, #a96bdc 100%); }"
        ".secondary { background-image: linear-gradient(to bottom, #f3f0f6 0%, #f3f0f6 20%, #c9c2d1 20%, #c9c2d1 54%, #aaa1b4 54%, #aaa1b4 100%); color: #28202f; border-radius: 2px; border: 2px solid #403849; padding: 8px 14px; font-weight: 700; }"
        ".secondary:hover { background: #ded6e7; }"
        ".danger-soft { background: #512e4c; color: #ffd9ef; border-radius: 2px; border: 2px solid #8b4f78; }"
        ".step-cell { background: #383340; border: 1px solid #19161e; border-radius: 0; color: white; padding: 0; box-shadow: inset 1px 1px #4b4554; }"
        ".step-cell:hover { background: #53455f; border-color: #c292e0; }"
        ".step-active { background-image: linear-gradient(to bottom, #d5a7f1 0%, #d5a7f1 25%, #9a58c7 25%, #9a58c7 70%, #66328e 70%, #66328e 100%); border-color: #e4c5f6; color: white; }"
        ".step-selected { border: 3px solid #fcf2ff; box-shadow: 0 0 0 2px #9b5ac7; }"
        ".tile-selected { border: 3px solid #67e6d2; box-shadow: 0 0 0 2px #245e62; }"
        ".drag-target { border: 3px dashed #ffd65c; background: #65572d; }"
        ".step-playing { border: 3px solid #f4d35e; box-shadow: 0 0 0 2px #3a2b08; }"
        ".step-number { color: #aaa0b4; font-size: 11px; padding: 4px; }"
        ".resize-grip { background-image: linear-gradient(to bottom, #b78adc 0%, #b78adc 25%, #70488e 25%, #70488e 75%, #4e3065 75%, #4e3065 100%); color: white; border: 2px solid #24172e; padding: 2px; }"
        ".resize-grip:hover { background: #c69bea; border-color: #f0d9ff; }"
        ".resize-add-preview { background-image: linear-gradient(135deg, #8df0df, #6f73dd); border: 2px solid #c8fff5; box-shadow: 0 0 0 2px #315b71; }"
        ".resize-remove-preview { background: #c44782; border: 2px solid #ffb9d9; box-shadow: 0 0 0 2px #5d1f3d; }"
        ".piano-white { background-image: linear-gradient(to right, #ffffff, #d9d4df); color: #211b27; border-radius: 1px; border: 2px solid #18151c; font-weight: 700; }"
        ".piano-black { background-image: linear-gradient(to right, #17141c, #44394e); color: white; border-radius: 1px; border: 2px solid #0c0a0e; font-weight: 700; }"
        "spinbutton { background: #18151d; color: #f1eaf5; border: 2px solid #756482; border-radius: 1px; padding: 6px; }"
        ".volume-label { color: #f1eaf5; font-size: 11px; font-weight: 700; min-width: 38px; }"
        "scale trough { background: #19161d; border: 1px solid #0d0b10; min-height: 8px; }"
        "scale highlight { background: #ad71d2; }"
        "scale slider { background-image: linear-gradient(to bottom, #ffffff, #9588a1); border: 1px solid #302838; border-radius: 1px; min-width: 10px; min-height: 18px; }"
        "notebook header { background: #211d27; border-bottom: 2px solid #7a5b91; } notebook tab { color: #bdb4c5; padding: 7px 12px; } notebook tab:checked { color: white; background: #604077; font-weight: 700; }"
        "statusbar { background: #17141c; color: #c7bdce; border-top: 2px solid #6f5980; }";

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Music Emporium");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 1120, 720);
    gtk_window_set_position(GTK_WINDOW(main_window), GTK_WIN_POS_CENTER);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(main_window, "key-press-event", G_CALLBACK(on_editor_key_press), NULL);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(main_window), root);
    main_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(main_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(main_stack), 240);
    gtk_box_pack_start(GTK_BOX(root), main_stack, TRUE, TRUE, 0);

    GtkWidget *home = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_halign(home, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(home, GTK_ALIGN_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(home), 32);
    GtkWidget *title = gtk_label_new("Music Emporium");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "hero-title");
    GtkWidget *subtitle = gtk_label_new("[ GAME AUDIO WORKSTATION // .ME FORMAT ]");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "hero-subtitle");
    GtkWidget *home_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_size_request(home_panel, 430, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(home_panel), "home-panel");
    GtkWidget *welcome = gtk_label_new("NEW PROJECT / LOAD ASSET");
    gtk_style_context_add_class(gtk_widget_get_style_context(welcome), "section-title");
    GtkWidget *hint = gtk_label_new("Build loops, cues and retro game melodies on a pixel grid.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "muted");
    GtkWidget *btn_new = gtk_button_new_with_label("＋  New composition");
    GtkWidget *btn_open = gtk_button_new_with_label("⌁  Open composition");
    GtkWidget *btn_quit = gtk_button_new_with_label("Quit");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_new), "primary");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_open), "secondary");

    g_signal_connect(btn_new, "clicked", G_CALLBACK(on_new_clicked), NULL);
    g_signal_connect(btn_open, "clicked", G_CALLBACK(on_open_clicked), NULL);
    g_signal_connect(btn_quit, "clicked", G_CALLBACK(on_quit_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(home_panel), welcome, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(home_panel), hint, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(home_panel), btn_new, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(home_panel), btn_open, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(home_panel), btn_quit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(home), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(home), subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(home), home_panel, FALSE, FALSE, 12);
    gtk_stack_add_named(GTK_STACK(main_stack), home, "home");

    GtkWidget *editor = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(editor), 18);
    GtkWidget *menu_bar = gtk_menu_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(menu_bar), "retro-menu");
    GtkWidget *file_item = gtk_menu_item_new_with_label("File");
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *menu_save = gtk_menu_item_new_with_label("Save");
    GtkWidget *menu_save_as = gtk_menu_item_new_with_label("Save As…");
    GtkWidget *menu_export_wav = gtk_menu_item_new_with_label("Export as WAV…");
    GtkWidget *menu_export_mp4 = gtk_menu_item_new_with_label("Export as MP4…");
    GtkWidget *menu_separator = gtk_separator_menu_item_new();
    GtkWidget *menu_library = gtk_menu_item_new_with_label("Return to Library");
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_save);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_save_as);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_export_wav);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_export_mp4);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), menu_library);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), file_item);
    g_signal_connect(menu_save, "activate", G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(menu_save_as, "activate", G_CALLBACK(on_save_as_clicked), NULL);
    g_signal_connect(menu_export_wav, "activate", G_CALLBACK(on_export_clicked), GINT_TO_POINTER(0));
    g_signal_connect(menu_export_mp4, "activate", G_CALLBACK(on_export_clicked), GINT_TO_POINTER(1));
    g_signal_connect(menu_library, "activate", G_CALLBACK(on_back_to_home), NULL);
    gtk_box_pack_start(GTK_BOX(editor), menu_bar, FALSE, FALSE, 0);

    GtkWidget *edit_item = gtk_menu_item_new_with_label("Edit");
    GtkWidget *edit_menu = gtk_menu_new();
    GtkWidget *menu_grid_size = gtk_menu_item_new_with_label("Grid Size…");
    GtkWidget *menu_add_column = gtk_menu_item_new_with_label("Add Column");
    GtkWidget *menu_remove_column = gtk_menu_item_new_with_label("Remove Column");
    GtkWidget *menu_add_row = gtk_menu_item_new_with_label("Add Row");
    GtkWidget *menu_remove_row = gtk_menu_item_new_with_label("Remove Row");
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), menu_grid_size);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), menu_add_column);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), menu_remove_column);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), menu_add_row);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), menu_remove_row);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_item), edit_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), edit_item);
    g_signal_connect(menu_grid_size, "activate", G_CALLBACK(on_grid_size_clicked), NULL);
    g_signal_connect(menu_add_column, "activate", G_CALLBACK(on_adjust_grid_clicked), GINT_TO_POINTER(0));
    g_signal_connect(menu_remove_column, "activate", G_CALLBACK(on_adjust_grid_clicked), GINT_TO_POINTER(1));
    g_signal_connect(menu_add_row, "activate", G_CALLBACK(on_adjust_grid_clicked), GINT_TO_POINTER(2));
    g_signal_connect(menu_remove_row, "activate", G_CALLBACK(on_adjust_grid_clicked), GINT_TO_POINTER(3));

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "retro-toolbar");
    GtkWidget *back = gtk_button_new_with_label("← Library");
    GtkWidget *save = gtk_button_new_with_label("Save");
    GtkWidget *previous = gtk_button_new_with_label("◀");
    play_button = gtk_button_new_with_label("▶  Play");
    GtkWidget *next = gtk_button_new_with_label("▶");
    GtkWidget *transport = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(transport), "transport");
    tool_buttons[TOOL_SELECT] = gtk_toggle_button_new_with_label("↖ Select [S]");
    tool_buttons[TOOL_BRUSH] = gtk_toggle_button_new_with_label("✎ Brush [B]");
    tool_buttons[TOOL_RECTANGLE] = gtk_toggle_button_new_with_label("▣ Rect [M]");
    tool_buttons[TOOL_DRAG] = gtk_toggle_button_new_with_label("✥ Drag [D]");
    for (int i = 0; i < 4; i++) {
        gtk_style_context_add_class(gtk_widget_get_style_context(tool_buttons[i]), "tool-button");
        g_signal_connect(tool_buttons[i], "toggled", G_CALLBACK(on_tool_toggled), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(transport), tool_buttons[i], FALSE, FALSE, 0);
    }
    changing_tool_buttons = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool_buttons[TOOL_SELECT]), TRUE);
    changing_tool_buttons = FALSE;
    GtkWidget *volume_icon = gtk_label_new("VOL");
    GtkWidget *volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_widget_set_size_request(volume_scale, 115, -1);
    gtk_scale_set_draw_value(GTK_SCALE(volume_scale), FALSE);
    gtk_range_set_value(GTK_RANGE(volume_scale), audio_get_volume() * 100.0);
    volume_label = gtk_label_new("75%");
    gtk_style_context_add_class(gtk_widget_get_style_context(volume_label), "volume-label");
    gtk_style_context_add_class(gtk_widget_get_style_context(back), "secondary");
    gtk_style_context_add_class(gtk_widget_get_style_context(save), "primary");
    gtk_style_context_add_class(gtk_widget_get_style_context(previous), "secondary");
    gtk_style_context_add_class(gtk_widget_get_style_context(play_button), "primary");
    gtk_style_context_add_class(gtk_widget_get_style_context(next), "secondary");
    editor_title = gtk_label_new("Composition");
    gtk_widget_set_halign(editor_title, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(editor_title), "hero-subtitle");
    gtk_box_pack_start(GTK_BOX(toolbar), back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), editor_title, TRUE, TRUE, 8);
    gtk_box_pack_start(GTK_BOX(transport), previous, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(transport), play_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transport), next, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transport), volume_icon, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(transport), volume_scale, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transport), volume_label, FALSE, FALSE, 2);
    gtk_box_pack_end(GTK_BOX(toolbar), save, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(toolbar), transport, FALSE, FALSE, 4);
    g_signal_connect(back, "clicked", G_CALLBACK(on_back_to_home), NULL);
    g_signal_connect(save, "clicked", G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(previous, "clicked", G_CALLBACK(on_previous_clicked), NULL);
    g_signal_connect(play_button, "clicked", G_CALLBACK(on_play_pause_clicked), NULL);
    g_signal_connect(next, "clicked", G_CALLBACK(on_next_clicked), NULL);
    g_signal_connect(volume_scale, "value-changed", G_CALLBACK(on_volume_changed), NULL);
    gtk_box_pack_start(GTK_BOX(editor), toolbar, FALSE, FALSE, 0);

    GtkWidget *workspace = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget *grid_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(grid_panel), "editor-panel");
    GtkWidget *grid_hint = gtk_label_new("PIANO ROLL  •  SELECT A STEP TO SHAPE ITS SOUND");
    gtk_widget_set_halign(grid_hint, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(grid_hint), "muted");
    GtkWidget *grid_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(grid_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    editor_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(editor_grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(editor_grid), 4);
    gtk_container_add(GTK_CONTAINER(grid_scroll), editor_grid);
    gtk_box_pack_start(GTK_BOX(grid_panel), grid_hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(grid_panel), grid_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(workspace), grid_panel, TRUE, TRUE, 0);

    GtkWidget *inspector = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_size_request(inspector, 260, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(inspector), "inspector");
    GtkWidget *inspector_title = gtk_label_new("Note details");
    gtk_style_context_add_class(gtk_widget_get_style_context(inspector_title), "section-title");
    selection_label = gtk_label_new("Select a step");
    gtk_style_context_add_class(gtk_widget_get_style_context(selection_label), "muted");
    GtkWidget *frequency_label = gtk_label_new("Frequency (Hz)");
    gtk_widget_set_halign(frequency_label, GTK_ALIGN_START);
    frequency_spin = gtk_spin_button_new_with_range(0, 2500, 1);
    GtkWidget *duration_label = gtk_label_new("Duration (ms)");
    gtk_widget_set_halign(duration_label, GTK_ALIGN_START);
    duration_spin = gtk_spin_button_new_with_range(0, 1000, 10);
    GtkWidget *control_hint = gtk_label_new("Tip: click a piano key to apply its pitch to the selected step.");
    gtk_label_set_line_wrap(GTK_LABEL(control_hint), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(control_hint), "muted");
    GtkWidget *clear = gtk_button_new_with_label("Clear this step");
    gtk_style_context_add_class(gtk_widget_get_style_context(clear), "danger-soft");
    g_signal_connect(frequency_spin, "value-changed", G_CALLBACK(on_note_value_changed), GINT_TO_POINTER(0));
    g_signal_connect(duration_spin, "value-changed", G_CALLBACK(on_note_value_changed), GINT_TO_POINTER(1));
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_cell), NULL);
    gtk_box_pack_start(GTK_BOX(inspector), inspector_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inspector), selection_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inspector), frequency_label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(inspector), frequency_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inspector), duration_label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(inspector), duration_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inspector), control_hint, FALSE, FALSE, 8);
    gtk_box_pack_end(GTK_BOX(inspector), clear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(workspace), inspector, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(editor), workspace, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(main_stack), editor, "editor");

    statusbar = gtk_statusbar_new();
    status_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "main");
    gtk_box_pack_end(GTK_BOX(root), statusbar, FALSE, FALSE, 0);
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "home");

    gtk_widget_show_all(main_window);
    return main_window;
}
