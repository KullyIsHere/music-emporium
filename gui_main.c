#include <gtk/gtk.h>
#include "gui.h"
#include "audio.h"

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    audio_init();

    GtkWidget *window = build_main_window();

    gtk_main();
    audio_close();
    return 0;
}
