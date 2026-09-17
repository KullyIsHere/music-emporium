#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "gui.h"
#include "music.h"
#include "audio.h"

#define MAX_RECENT_FILES 12
#define DISCOVERY_DEPTH 3

enum {
    FILE_COL_NAME,
    FILE_COL_LOCATION,
    FILE_COL_PATH,
    FILE_N_COLUMNS
};

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
static void remember_recent_file(const char *path);

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
    if (app_state.music[0].music_data[x][y][0] > 0 &&
        app_state.music[0].music_data[x][y][1] > 0) {
        gtk_style_context_add_class(context, "step-active");
        gtk_button_set_label(GTK_BUTTON(button), "●");
    } else {
        gtk_button_set_label(GTK_BUTTON(button), "");
    }
    if (x == selected_x && y == selected_y)
        gtk_style_context_add_class(context, "step-selected");
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
    select_cell(packed / 10, packed % 10);
}

static void on_note_value_changed(GtkSpinButton *spin, gpointer user_data) {
    if (updating_controls || selected_x < 0 || selected_y < 0)
        return;

    int value_index = GPOINTER_TO_INT(user_data);
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

static void rebuild_editor(void) {
    pause_playback();
    show_playhead(-1);
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
            gtk_grid_attach(GTK_GRID(editor_grid), cell, x + 1, y + 1, 1, 1);
            cell_buttons[x][y] = cell;
            refresh_cell(x, y);
        }
    }
    gtk_widget_show_all(editor_grid);
    if (music->columns > 0 && music->rows > 0)
        select_cell(0, 0);
}

static void show_editor(void) {
    rebuild_editor();
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
        "window { background-image: linear-gradient(135deg, #24103f, #6f32b5 55%, #b894e8); color: #24103f; }"
        ".home-panel, .editor-panel, .inspector { background: rgba(255,255,255,0.96); border-radius: 18px; padding: 22px; box-shadow: 0 8px 24px rgba(28,8,48,0.28); }"
        ".hero-title { color: #ffffff; font-size: 40px; font-weight: 800; }"
        ".hero-subtitle { color: #eee3ff; font-size: 17px; }"
        ".section-title { color: #47206f; font-size: 22px; font-weight: 700; }"
        ".muted { color: #745f86; }"
        ".primary { background-image: linear-gradient(to bottom, #9558dc, #6425a5); color: white; border-radius: 10px; border: 1px solid #57208f; padding: 10px 18px; font-weight: 700; }"
        ".primary:hover { background-image: linear-gradient(to bottom, #a96bea, #7532bb); }"
        ".secondary { background: #f5effc; color: #5b258e; border-radius: 10px; border: 1px solid #d8c2ee; padding: 10px 18px; }"
        ".danger-soft { background: #fff1fa; color: #8a2866; border-radius: 8px; border: 1px solid #edc7df; }"
        ".step-cell { background: #f7f2fb; border: 1px solid #e4d8ee; border-radius: 7px; color: white; padding: 0; }"
        ".step-cell:hover { background: #eaddf6; border-color: #a96be0; }"
        ".step-active { background-image: linear-gradient(135deg, #b266ed, #6c29ad); border-color: #5b218f; color: white; }"
        ".step-selected { border: 3px solid #f0b7ff; box-shadow: 0 0 0 2px #6624a4; }"
        ".step-playing { border: 3px solid #ffcf5a; box-shadow: 0 0 0 2px #8b5b00; }"
        ".step-number { color: #806a91; font-size: 11px; padding: 4px; }"
        ".piano-white { background: white; color: #2d173d; border-radius: 4px; border: 1px solid #cfc4d7; font-weight: 700; }"
        ".piano-black { background-image: linear-gradient(to right, #271934, #4f3564); color: white; border-radius: 4px; border: 1px solid #1f1328; font-weight: 700; }"
        "spinbutton { background: white; color: #3c2051; border: 1px solid #cdb5e2; border-radius: 8px; padding: 7px; }"
        "notebook header { background: #f3eafb; } notebook tab:checked { color: #6425a5; font-weight: 700; }"
        "statusbar { background: rgba(255,255,255,0.92); color: #55346b; }";

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
    GtkWidget *subtitle = gtk_label_new("Shape a melody, one luminous step at a time.");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "hero-subtitle");
    GtkWidget *home_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_size_request(home_panel, 430, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(home_panel), "home-panel");
    GtkWidget *welcome = gtk_label_new("Start a composition");
    gtk_style_context_add_class(gtk_widget_get_style_context(welcome), "section-title");
    GtkWidget *hint = gtk_label_new("Create a fresh grid or continue from a .me file.");
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
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *back = gtk_button_new_with_label("← Library");
    GtkWidget *save = gtk_button_new_with_label("Save");
    GtkWidget *previous = gtk_button_new_with_label("◀");
    play_button = gtk_button_new_with_label("▶  Play");
    GtkWidget *next = gtk_button_new_with_label("▶");
    GtkWidget *transport = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
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
    gtk_box_pack_start(GTK_BOX(transport), previous, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transport), play_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(transport), next, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(toolbar), save, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(toolbar), transport, FALSE, FALSE, 4);
    g_signal_connect(back, "clicked", G_CALLBACK(on_back_to_home), NULL);
    g_signal_connect(save, "clicked", G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(previous, "clicked", G_CALLBACK(on_previous_clicked), NULL);
    g_signal_connect(play_button, "clicked", G_CALLBACK(on_play_pause_clicked), NULL);
    g_signal_connect(next, "clicked", G_CALLBACK(on_next_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(editor), toolbar, FALSE, FALSE, 0);

    GtkWidget *workspace = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget *grid_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(grid_panel), "editor-panel");
    GtkWidget *grid_hint = gtk_label_new("PIANO ROLL  •  Select any step to shape its sound");
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
