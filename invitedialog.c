#include "invitedialog.h"
#include "globals.h"
#include "protocol/yamp.h"
#include <stdlib.h>
#include <string.h>

#define INVITE_MAX_USES 320

typedef struct {
	char* space_id;
	GtkWidget* window;
	GtkWidget* uses_spin;
	GtkWidget* gen_btn;
	GtkWidget* result_box;
	GtkWidget* link_entry;
	GtkWidget* copy_btn;
	GtkWidget* status_lbl;
} InviteDialog;

typedef struct {
	GWeakRef win;
	char* code;
	int success;
} InviteResult;

static void InviteDialogFree(gpointer p) {
	InviteDialog* d = p;
	g_free(d->space_id);
	g_free(d);
}

static char* BuildInviteLink(const char* code) {
	const char* base = YAMPGetHTTPBaseURI();
	if (!base || !*base)
		return g_strdup(code);
	return g_strdup_printf("%s/invite/%s", base, code);
}

static gboolean InviteResultMain(gpointer data) {
	InviteResult* r = data;
	GtkWidget* win = g_weak_ref_get(&r->win);

	if (win) {
		InviteDialog* d = g_object_get_data(G_OBJECT(win), "invite-dialog");
		gtk_widget_set_sensitive(d->gen_btn, TRUE);

		if (r->success && r->code) {
			char* link = BuildInviteLink(r->code);
			gtk_editable_set_text(GTK_EDITABLE(d->link_entry), link);
			g_free(link);
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
	InviteDialog* d = data;
	int uses = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->uses_spin));

	InviteResult* r = g_new0(InviteResult, 1);
	g_weak_ref_init(&r->win, d->window);

	gtk_widget_set_sensitive(d->gen_btn, FALSE);
	gtk_label_set_text(GTK_LABEL(d->status_lbl), "");
	YAMPCreateSpaceInvite(mainsock, d->space_id, uses, OnInviteCreated, r);
}

static void OnCopy(GtkButton* btn, gpointer data) {
	InviteDialog* d = data;
	gdk_clipboard_set_text(gtk_widget_get_clipboard(d->link_entry),
						   gtk_editable_get_text(GTK_EDITABLE(d->link_entry)));
	gtk_button_set_label(btn, "Copied!");
}

static void OnClose(GtkButton* btn, gpointer data) {
	gtk_window_destroy(GTK_WINDOW(data));
}

void SpawnInviteDialog(GtkWindow* parent, const char* space_id,
					   const char* space_name) {
	InviteDialog* d = g_new0(InviteDialog, 1);
	d->space_id = g_strdup(space_id);

	GtkWidget* win = gtk_window_new();
	d->window = win;
	gtk_window_set_title(GTK_WINDOW(win), "Invite people");
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

	char* title = g_markup_printf_escaped(
		"<span size=\"large\" weight=\"bold\">Invite people to %s</span>",
		space_name ? space_name : "this space");
	GtkWidget* titlelbl = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(titlelbl), title);
	g_free(title);
	gtk_label_set_xalign(GTK_LABEL(titlelbl), 0.0f);
	gtk_label_set_wrap(GTK_LABEL(titlelbl), TRUE);
	gtk_box_append(GTK_BOX(box), titlelbl);

	GtkWidget* usesrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget* useslbl = gtk_label_new("Max uses");
	gtk_widget_set_hexpand(useslbl, TRUE);
	gtk_label_set_xalign(GTK_LABEL(useslbl), 0.0f);
	d->uses_spin = gtk_spin_button_new_with_range(0, INVITE_MAX_USES, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->uses_spin), 0);
	gtk_box_append(GTK_BOX(usesrow), useslbl);
	gtk_box_append(GTK_BOX(usesrow), d->uses_spin);
	gtk_box_append(GTK_BOX(box), usesrow);

	GtkWidget* hint = gtk_label_new("0 means unlimited uses");
	gtk_widget_set_opacity(hint, 0.6f);
	gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
	gtk_box_append(GTK_BOX(box), hint);

	d->gen_btn = gtk_button_new_with_label("Create invite");
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

	GtkWidget* closebtn = gtk_button_new_with_label("Close");
	gtk_widget_set_halign(closebtn, GTK_ALIGN_END);
	g_signal_connect(closebtn, "clicked", G_CALLBACK(OnClose), win);
	gtk_box_append(GTK_BOX(box), closebtn);

	gtk_window_present(GTK_WINDOW(win));
}