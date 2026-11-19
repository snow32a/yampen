#include "invitedialog.h"
#include "globals.h"
#include "protocol/yamp.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
	char* gc_id;
	GtkWidget* window;
	GtkWidget* uses_spin;
	GtkWidget* gen_btn;
	GtkWidget* result_box;
	GtkWidget* link_entry;
	GtkWidget* copy_btn;
	GtkWidget* status_lbl;
} GCEditDialog;

typedef struct {
	GWeakRef win;
	char* code;
	int success;
} InviteResult;

static void InviteDialogFree(gpointer p) {
	GCEditDialog* d = p;
	free(d->gc_id);
	g_free(d);
}

static gboolean InviteResultMain(gpointer data) {
	InviteResult* r = data;
	GtkWidget* win = g_weak_ref_get(&r->win);

	if (win) {
		GCEditDialog* d = g_object_get_data(G_OBJECT(win), "invite-dialog");
		gtk_widget_set_sensitive(d->gen_btn, TRUE);

		if (r->success && r->code) {
			// gtk_editable_set_text(GTK_EDITABLE(d->link_entry), link);
			gtk_button_set_label(GTK_BUTTON(d->copy_btn), "Copy");
			gtk_label_set_text(GTK_LABEL(d->status_lbl), "");
			gtk_widget_set_visible(d->result_box, TRUE);
			gtk_widget_grab_focus(d->link_entry);
			gtk_editable_select_region(GTK_EDITABLE(d->link_entry), 0, -1);
		} else {
			gtk_label_set_text(GTK_LABEL(d->status_lbl),
							   "Couldn't create the invite.");
		}
		g_object_unref(win);
	}

	g_weak_ref_clear(&r->win);
	g_free(r->code);
	g_free(r);
	return G_SOURCE_REMOVE;
}

static void OnInviteCreated(char* code, int success, void* userdata) {
	InviteResult* r = userdata;
	r->code = g_strdup(code);
	r->success = success;
	g_idle_add(InviteResultMain, r);
}

static void OnGenerate(GtkButton* btn, gpointer data) {
	GCEditDialog* d = data;
	int uses = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->uses_spin));

	InviteResult* r = g_new0(InviteResult, 1);
	g_weak_ref_init(&r->win, d->window);

	gtk_widget_set_sensitive(d->gen_btn, FALSE);
	gtk_label_set_text(GTK_LABEL(d->status_lbl), "");
	// YAMPCreateSpaceInvite(mainsock, d->space_id, uses, OnInviteCreated, r);
}

static void OnCopy(GtkButton* btn, gpointer data) {
	GCEditDialog* d = data;
	gdk_clipboard_set_text(gtk_widget_get_clipboard(d->link_entry),
						   gtk_editable_get_text(GTK_EDITABLE(d->link_entry)));
	gtk_button_set_label(btn, "Copied!");
}

static void OnClose(GtkButton* btn, gpointer data) {
	gtk_window_destroy(GTK_WINDOW(data));
}

void SpawnGCEdit(GtkWindow* parent, const char* gc_id,
					   const char* gc_name, const char* gc_icon) {
	GCEditDialog* d = g_new0(GCEditDialog, 1);
	d->gc_id = strdup(gc_id);

	GtkWidget* win = gtk_window_new();
	d->window = win;
	gtk_window_set_title(GTK_WINDOW(win), "Groupchat Settings");
	gtk_window_set_transient_for(GTK_WINDOW(win), parent);
	gtk_window_set_modal(GTK_WINDOW(win), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(win), 380, -1);
	g_object_set_data_full(G_OBJECT(win), "invite-dialog", d, InviteDialogFree);

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_window_set_child(GTK_WINDOW(win), box);

	GtkWidget* titlelbl = gtk_label_new("Customize this GC");
	gtk_label_set_xalign(GTK_LABEL(titlelbl), 0.0f);
	gtk_label_set_wrap(GTK_LABEL(titlelbl), TRUE);
	gtk_box_append(GTK_BOX(box), titlelbl);


	d->gen_btn = gtk_button_new_with_label("Apply");
	g_signal_connect(d->gen_btn, "clicked", G_CALLBACK(OnGenerate), d);
	gtk_box_append(GTK_BOX(box), d->gen_btn);

	d->result_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	d->link_entry = gtk_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(d->link_entry), FALSE);
	gtk_widget_set_hexpand(d->link_entry, TRUE);
	d->copy_btn = gtk_button_new_with_label("Copy");
	g_signal_connect(d->copy_btn, "clicked", G_CALLBACK(OnCopy), d);
	gtk_box_append(GTK_BOX(d->result_box), d->link_entry);
	gtk_box_append(GTK_BOX(d->result_box), d->copy_btn);
	gtk_widget_set_visible(d->result_box, FALSE);
	gtk_box_append(GTK_BOX(box), d->result_box);

	d->status_lbl = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(d->status_lbl), 0.0f);
	gtk_box_append(GTK_BOX(box), d->status_lbl);

	GtkWidget* closebtn = gtk_button_new_with_label("Cancel");
	gtk_widget_set_halign(closebtn, GTK_ALIGN_END);
	g_signal_connect(closebtn, "clicked", G_CALLBACK(OnClose), win);
	gtk_box_append(GTK_BOX(box), closebtn);

	gtk_window_present(GTK_WINDOW(win));
}