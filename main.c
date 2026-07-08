#include <stdio.h>
#include <gtk/gtk.h>
#include "glib.h"
#include "login.h"
#include "imwnd.h"
#include "globals.h"
#include <cjson/cJSON.h>
#include "hashtables.h"

GQueue *spaces_queue = NULL;
GtkApplication *global_app;
static void activate(GtkApplication *app, gpointer user_data) {
	DisplayLoginDialog(app);
	/*
	GtkWidget *window;

	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Yampen");
	gtk_window_set_default_size(GTK_WINDOW(window), 200, 300);
	gtk_window_present(GTK_WINDOW(window));
	GMenu *menu = g_menu_new();
*/
}

int main(int argc, char **argv) {
	curl = curl_easy_init();
	spaces_queue = g_queue_new();
	InitAllTables();
	int status;

	global_app = gtk_application_new("xyz.defautluser0.yamp", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(global_app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(global_app), argc, argv);
	g_object_unref(global_app);
	return status;
}
