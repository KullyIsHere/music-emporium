#include <gtk/gtk.h>
#include "gui.h"
#include "music.h"

//Holds the loaded music data and current state for the GUI
typedef struct {
    MusicFile music[99];
    current_data current;
} AppState;

//Shared widgets and state used by the signal handlers
static GtkWidget *main_window;
static GtkWidget *statusbar;
static guint status_ctx;
static AppState app_state;

//Shows a message in the status bar at the bottom of the window
static void show_status(const char *message) {
    gtk_statusbar_push(GTK_STATUSBAR(statusbar), status_ctx, message);
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

            app_state.current.file_index = 0;
            strcpy(app_state.current.file, name);

            char msg[512];
            snprintf(msg, sizeof(msg), "Created %s.me (%d rows, %d columns).",
                name, rows, columns);
            show_status(msg);

            gtk_window_set_title(GTK_WINDOW(main_window), "Music Emporium - new file");
        }
    }

    gtk_widget_destroy(dialog);
}

//Open file button - lets the user pick a .me file and loads it
static void on_open_clicked(GtkButton *button, gpointer data) {
    //Builds the dialog with explicit calls instead of a varargs constructor
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Open File");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(main_window));
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Open", GTK_RESPONSE_ACCEPT);

    //Adds a file chooser widget into the dialog
    GtkWidget *chooser = gtk_file_chooser_widget_new(GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), chooser, TRUE, TRUE, 0);

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

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if (filename != NULL) {
            //Loads it into the first slot and reports what was opened
            app_state.current.file_sum = 1;
            strcpy(app_state.current.file_list[0], filename);

            if (load_music_file(app_state.music, app_state.current)) {
                app_state.current.file_index = 0;
                strcpy(app_state.current.file, filename);

                char msg[512];
                snprintf(msg, sizeof(msg), "Opened %s (%d rows, %d columns).",
                    filename, app_state.music[0].rows, app_state.music[0].columns);
                show_status(msg);

                gtk_window_set_title(GTK_WINDOW(main_window), "Music Emporium - file open");
            }

            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
}

//Builds the main menu window and returns it.
GtkWidget *build_main_window(void) {
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Music Emporium");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 600, 420);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    //Outer box: everything is laid out vertically
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    //Title label
    GtkWidget *title = gtk_label_new("Music Emporium");
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_scale_new(2.5));
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    //Subtitle
    GtkWidget *subtitle = gtk_label_new("Create and play your own music");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    //Buttons
    GtkWidget *btn_new = gtk_button_new_with_label("New File");
    GtkWidget *btn_open = gtk_button_new_with_label("Open File");
    GtkWidget *btn_quit = gtk_button_new_with_label("Quit");

    g_signal_connect(btn_new, "clicked", G_CALLBACK(on_new_clicked), NULL);
    g_signal_connect(btn_open, "clicked", G_CALLBACK(on_open_clicked), NULL);
    g_signal_connect(btn_quit, "clicked", G_CALLBACK(on_quit_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(vbox), btn_new, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_open, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_quit, FALSE, FALSE, 0);

    //Status bar for messages
    statusbar = gtk_statusbar_new();
    status_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "main");
    gtk_box_pack_end(GTK_BOX(vbox), statusbar, FALSE, FALSE, 0);

    gtk_widget_show_all(main_window);
    return main_window;
}
