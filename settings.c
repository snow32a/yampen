#include "globals.h"
#include <gtk/gtk.h>
GtkWidget* settings_window;
void SpawnSettings(){
    settings_window = gtk_application_window_new(global_app);
    gtk_window_set_title(GTK_WINDOW(settings_window), "Settings");
    gtk_window_set_default_size(GTK_WINDOW(settings_window), 600, 500);
}
