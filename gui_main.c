#include <gtk/gtk.h>
#include "gui.h"

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = build_main_window();

    gtk_main();
    return 0;
}
