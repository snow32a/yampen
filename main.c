#include <stdio.h>
#include <gtk/gtk.h>
#include "glib.h"
#include "gtk/gtkcssprovider.h"
#include "login.h"
#include "imwnd.h"
#include "globals.h"
#include <cjson/cJSON.h>
#include "hashtables.h"

GQueue* spaces_queue = NULL;
GtkApplication* global_app;
static void activate(GtkApplication* app, gpointer user_data) {
	GtkCssProvider* provider = gtk_css_provider_new();

	gtk_css_provider_load_from_resource(provider,
										"/org/yampen/assets/styles.css");

	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	g_object_unref(provider);

#ifdef FORCE_ADWAITA_DARK
	g_object_set(gtk_settings_get_default(), "gtk-theme-name", "Adwaita",
				 "gtk-application-prefer-dark-theme", TRUE, NULL);
#endif

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

int main(int argc, char** argv) {
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl = curl_easy_init();
	spaces_queue = g_queue_new();
	InitAllTables();
	int status;

	global_app =
		gtk_application_new("xyz.snow32.yampen", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(global_app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(global_app), argc, argv);
	g_object_unref(global_app);
	return status;
}