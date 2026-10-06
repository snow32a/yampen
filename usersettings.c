#include <gtk/gtk.h>
#include "globals.h"
#include "protocol/yamp.h"
#include "gtk/gtkshortcut.h"
typedef struct {
	GtkWidget* dpyname;
} ProfilePagePayload;
void ProfilePageApply(GtkWidget* btn, void* userdat) {
	ProfilePagePayload* payload = userdat;
	YampUser newprofile = {0};
	newprofile.displayname = gtk_entry_buffer_get_text(
		gtk_entry_get_buffer(GTK_ENTRY(payload->dpyname)));
	YAMPUpdateUserProfile(mainsock, newprofile);
}
void BuildProfilePage(GtkWidget* page) {
	GtkWidget* contentbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_vexpand(contentbox, 1);
	GtkWidget* btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(btnbox, GTK_ALIGN_END);


	gtk_widget_set_margin_top(page, 10);
	gtk_widget_set_margin_bottom(page, 10);
	gtk_widget_set_margin_start(page, 10);
	gtk_widget_set_margin_end(page, 10);
	GtkWidget* title = gtk_label_new(NULL);
	gtk_box_append(GTK_BOX(contentbox), title);
	gtk_label_set_markup(
		GTK_LABEL(title),
		"<span size=\"large\" weight=\"bold\">Your Profile</span>");
	GtkWidget* dpynamelbl = gtk_label_new("Display name");
	gtk_widget_set_hexpand(dpynamelbl, 1);
	gtk_label_set_xalign(GTK_LABEL(dpynamelbl), 0.0f);
	gtk_box_append(GTK_BOX(contentbox), dpynamelbl);
	GtkWidget* dpynameentry = gtk_entry_new();
	gtk_box_append(GTK_BOX(contentbox), dpynameentry);
	GtkWidget* desclbl = gtk_label_new("Description");
	gtk_widget_set_hexpand(desclbl, 1);
	gtk_label_set_xalign(GTK_LABEL(desclbl), 0.0f);
	gtk_box_append(GTK_BOX(contentbox), desclbl);
    GtkWidget* descscroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(descscroll, 0);
	GtkWidget* descentry = gtk_text_view_new();
    gtk_widget_set_hexpand(descentry, 1);
    gtk_widget_set_vexpand(descentry, 1);
    gtk_widget_set_size_request(descscroll, 300, 200);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(descscroll), descentry);
	gtk_box_append(GTK_BOX(contentbox), descscroll);
	gtk_box_append(GTK_BOX(page), contentbox);
	GtkWidget* applybtn = gtk_button_new_with_label("Apply");
	ProfilePagePayload* payload = malloc(sizeof(ProfilePagePayload));
	payload->dpyname = dpynameentry;
	g_signal_connect(applybtn, "clicked", G_CALLBACK(ProfilePageApply),
					 payload);
	gtk_box_append(GTK_BOX(btnbox), applybtn);
	gtk_box_append(GTK_BOX(page), btnbox);
}
void LaunchUserSettings(GtkButton* btn, void* dat) {
	GtkWidget* wnd = gtk_application_window_new(global_app);
	gtk_window_set_default_size(GTK_WINDOW(wnd), 800, 500);
	gtk_window_set_title(GTK_WINDOW(wnd), "User Settings");


	GtkWidget* page0 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_widget_set_hexpand(page0, 1);
	gtk_widget_set_vexpand(page0, 1);
	BuildProfilePage(page0);
	GtkWidget* page1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	GtkWidget* page2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);


	GtkWidget* stack = gtk_stack_new();
	gtk_stack_add_titled(GTK_STACK(stack), page0, "0", "Profile");
	gtk_stack_add_titled(GTK_STACK(stack), page1, "1", "User Account");
	gtk_stack_add_titled(GTK_STACK(stack), page2, "2", "Appearance");
	gtk_stack_add_titled(GTK_STACK(stack), page2, "3", "Language & Time");
	gtk_stack_add_titled(GTK_STACK(stack), page2, "4", "Log Out");

	GtkWidget* sw = gtk_stack_switcher_new();
	gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(sw), GTK_STACK(stack));
	gtk_orientable_set_orientation(GTK_ORIENTABLE(sw),
								   GTK_ORIENTATION_VERTICAL);

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(box), sw);
	gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
	gtk_box_append(GTK_BOX(box), stack);

	gtk_window_set_child(GTK_WINDOW(wnd), box);
	gtk_window_present(GTK_WINDOW(wnd));
}