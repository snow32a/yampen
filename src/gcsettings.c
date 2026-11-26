#include "invitedialog.h"
#include "globals.h"
#include <yamp.h>
#include <stdlib.h>
#include <string.h>
#include <avatar.h>
#include <helpers.h>

typedef struct {
	char* gc_id;
	char* pending_pfp; // local path picked by the user, NULL if unchanged
	char* initial_name;
	GtkWidget* window;
	GtkWidget* pfp_img;
	GtkWidget* name_entry;
	GtkWidget* apply_btn;
} GCEditDialog;

static void GCEditFree(gpointer p) {
	GCEditDialog* d = p;
	free(d->gc_id);
	g_free(d->pending_pfp);
	g_free(d);
}

static void OnPfpChosen(GObject* src, GAsyncResult* res, gpointer data) {
	GCEditDialog* d = data;
	GError* err = NULL;
	GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
	if (!file) {
		g_clear_error(&err); // cancelled or failed
		return;
	}
	char* path = g_file_get_path(file);
	g_object_unref(file);
	if (!path)
		return;

	g_free(d->pending_pfp);
	d->pending_pfp = path;
	gtk_image_set_from_file(GTK_IMAGE(d->pfp_img), path);
	gtk_image_set_pixel_size(GTK_IMAGE(d->pfp_img), 80);
}

static void OnPickPfp(GtkButton* btn, gpointer data) {
	GCEditDialog* d = data;

	GtkFileFilter* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Images");
	gtk_file_filter_add_mime_type(filter, "image/*");
	GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	g_list_store_append(filters, filter);
	g_object_unref(filter);

	GtkFileDialog* fd = gtk_file_dialog_new();
	gtk_file_dialog_set_title(fd, "Choose a group picture");
	gtk_file_dialog_set_filters(fd, G_LIST_MODEL(filters));
	g_object_unref(filters);

	gtk_file_dialog_open(fd, GTK_WINDOW(d->window), NULL, OnPfpChosen, d);
	g_object_unref(fd);
}

static void OnApply(GtkButton* btn, gpointer data) {
	GCEditDialog* d = data;
	const char* name = gtk_editable_get_text(GTK_EDITABLE(d->name_entry));

	gtk_widget_set_sensitive(d->apply_btn, FALSE);
	char* icon = YAMP_UNCHANGED;
	
	const char* cur = gtk_editable_get_text(GTK_EDITABLE(d->name_entry));
	if(strlen(cur)==0){
		cur=NULL;
	}

	YAMPGCEdit(mainsock, d->gc_id, cur, icon, NULL, NULL);
	gtk_window_destroy(GTK_WINDOW(d->window));
}

static void OnClose(GtkButton* btn, gpointer data) {
	gtk_window_destroy(GTK_WINDOW(data));
}

void SpawnGCEdit(GtkWindow* parent, const char* gc_id) {
	GCEditDialog* d = g_new0(GCEditDialog, 1);
	d->gc_id = strdup(gc_id);

	GtkWidget* win = gtk_window_new();
	d->window = win;
	gtk_window_set_title(GTK_WINDOW(win), "Group Chat Settings");
	gtk_window_set_transient_for(GTK_WINDOW(win), parent);
	gtk_window_set_modal(GTK_WINDOW(win), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(win), 380, -1);
	g_object_set_data_full(G_OBJECT(win), "gc-edit-dialog", d, GCEditFree);

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_window_set_child(GTK_WINDOW(win), box);

	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_box_append(GTK_BOX(box), row);

	char pfppath[38];
	snprintf(pfppath, sizeof pfppath, "/org/yampen/assets/pfps/default%d.png",
			 GetDefaultPfp((char*)gc_id));
	d->pfp_img = gtk_image_new_from_resource(pfppath);
	gtk_image_set_pixel_size(GTK_IMAGE(d->pfp_img), 80);
	gtk_widget_add_css_class(d->pfp_img, "avatar");
	gtk_widget_set_overflow(d->pfp_img, GTK_OVERFLOW_HIDDEN);

	GtkWidget* pfp_btn = gtk_button_new();
	gtk_button_set_child(GTK_BUTTON(pfp_btn), d->pfp_img);
	gtk_widget_add_css_class(pfp_btn, "flat");
	gtk_widget_set_valign(pfp_btn, GTK_ALIGN_START);
	gtk_widget_set_tooltip_text(pfp_btn, "Change group picture");
	g_signal_connect(pfp_btn, "clicked", G_CALLBACK(OnPickPfp), d);
	gtk_box_append(GTK_BOX(row), pfp_btn);

	GtkWidget* fields = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_hexpand(fields, TRUE);
	gtk_widget_set_valign(fields, GTK_ALIGN_CENTER);

	GtkWidget* name_lbl = gtk_label_new("Group name");
	gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0f);
	gtk_widget_add_css_class(name_lbl, "b");
	gtk_box_append(GTK_BOX(fields), name_lbl);

	d->name_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(d->name_entry), "Unnamed group");
	gtk_widget_set_hexpand(d->name_entry, TRUE);
	gtk_box_append(GTK_BOX(fields), d->name_entry);
	d->initial_name = "";

	gtk_box_append(GTK_BOX(row), fields);

	GtkWidget* btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(btns, GTK_ALIGN_END);

	GtkWidget* closebtn = gtk_button_new_with_label("Cancel");
	g_signal_connect(closebtn, "clicked", G_CALLBACK(OnClose), win);
	gtk_box_append(GTK_BOX(btns), closebtn);

	d->apply_btn = gtk_button_new_with_label("Apply");
	gtk_widget_add_css_class(d->apply_btn, "suggested-action");
	g_signal_connect(d->apply_btn, "clicked", G_CALLBACK(OnApply), d);
	gtk_box_append(GTK_BOX(btns), d->apply_btn);

	gtk_box_append(GTK_BOX(box), btns);

	gtk_window_set_default_widget(GTK_WINDOW(win), d->apply_btn);
	gtk_window_present(GTK_WINDOW(win));
}