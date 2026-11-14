#include <gtk/gtk.h>
#include "globals.h"
#include "protocol/yamp.h"
#include "gtk/gtkshortcut.h"
#include "chatwnd.h"
#include "imwnd.h"
#include "hashtables.h"

typedef struct {
	GtkWidget* dpyname;
	GtkWidget* desc;
	GtkWidget* applybtn;
	GtkWidget* pfp;

	char* username;
	char* orig_dpyname;
	char* orig_desc;
	char* new_pfp_path;
} ProfilePagePayload;

char* NormalizeText(const char* s) {
	if (!s)
		return NULL;
	char* c = g_strstrip(g_strdup(s));
	if (!*c) {
		g_free(c);
		return NULL;
	}
	return c;
}

char* ReadDisplayName(ProfilePagePayload* p) {
	char* t = NormalizeText(gtk_editable_get_text(GTK_EDITABLE(p->dpyname)));
	if (t && !p->orig_dpyname && p->username && !strcmp(t, p->username)) {
		g_free(t);
		return NULL;
	}
	return t;
}

char* ReadDescription(ProfilePagePayload* p) {
	GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(p->desc));
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(buf, &start, &end);
	char* raw = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
	char* out = NormalizeText(raw);
	g_free(raw);
	return out;
}

void ProfilePageRefresh(ProfilePagePayload* p) {
	char* dn = ReadDisplayName(p);
	char* ds = ReadDescription(p);

	gboolean changed = g_strcmp0(dn, p->orig_dpyname) != 0 ||
					   g_strcmp0(ds, p->orig_desc) != 0 ||
					   p->new_pfp_path != NULL;
	gtk_widget_set_sensitive(p->applybtn, changed);

	g_free(dn);
	g_free(ds);
}

void ProfilePageChanged(GtkWidget* w, gpointer userdat) {
	ProfilePageRefresh(userdat);
}

void ProfilePageFree(GtkWidget* w, gpointer userdat) {
	ProfilePagePayload* p = userdat;
	g_free(p->username);
	g_free(p->orig_dpyname);
	g_free(p->orig_desc);
	g_free(p->new_pfp_path);
	free(p);
}

void OnPfpChosen(GObject* src, GAsyncResult* res, gpointer data) {
	ProfilePagePayload* p = data;
	GError* err = NULL;
	GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
	if (!file) {
		g_clear_error(&err);
		return;
	}

	g_free(p->new_pfp_path);
	p->new_pfp_path = g_file_get_path(file);
	g_object_unref(file);

	gtk_image_set_from_file(GTK_IMAGE(p->pfp), p->new_pfp_path);
	gtk_image_set_pixel_size(GTK_IMAGE(p->pfp), 80);

	ProfilePageRefresh(p);
}

void OnPfpClicked(GtkGestureClick* g, int n_press, double x, double y,
						 gpointer data) {
	ProfilePagePayload* p = data;

	GtkFileFilter* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Images");
	gtk_file_filter_add_mime_type(filter, "image/*");
	GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	g_list_store_append(filters, filter);
	g_object_unref(filter);

	GtkFileDialog* dlg = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dlg, "Choose profile picture");
	gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));
	g_object_unref(filters);

	GtkRoot* root = gtk_widget_get_root(p->pfp);
	gtk_file_dialog_open(dlg, GTK_WINDOW(root), NULL, OnPfpChosen, p);
	g_object_unref(dlg);
}
typedef struct {
	unsigned char* data;  // kept alive until the upload completes
	int len;
} PfpUploadCtx;

void OnPfpUploaded(char* url, int success, void* userdat) {
	PfpUploadCtx* ctx = userdat;

	if (success && url) {
		YampUser upd = {0};
		upd.pfp = url;
		YAMPUpdateUserProfile(mainsock, &upd, NULL);
	} else {
		g_warning("Profile picture upload failed");
	}

	g_free(ctx->data);
	g_free(ctx);
}
gboolean StartPfpUpload(const char* path) {
	unsigned char* data = NULL;
	gsize len = 0;
	GError* err = NULL;

	if (!g_file_get_contents(path, (gchar**)&data, &len, &err)) {
		g_warning("failed to get pfp %s: %s", path, err->message);
		g_clear_error(&err);
		return FALSE;
	}
	gboolean uncertain = FALSE;
	char* ctype = g_content_type_guess(path, data, len, &uncertain);
	char* mime = g_content_type_get_mime_type(ctype);
	g_free(ctype);

	if (!mime || strncmp(mime, "image/", 6) != 0) {
		g_warning("%s isnt an image (%s)", path, mime ? mime : "unknown");
		g_free(mime);
		g_free(data);
		return FALSE;
	}

	PfpUploadCtx* ctx = g_new0(PfpUploadCtx, 1);
	ctx->data = data;
	ctx->len = (int)len;

	YAMPUploadFile(mainsock, mime, ctx->data, ctx->len, OnPfpUploaded, ctx);
	g_free(mime);
	return TRUE;
}


void ProfilePageApply(GtkWidget* btn, void* userdat) {
	ProfilePagePayload* p = userdat;

	char* dn = ReadDisplayName(p);
	char* ds = ReadDescription(p);

	YampUser newprofile = {0};
	gboolean text_changed = FALSE;

	if (g_strcmp0(dn, p->orig_dpyname)) {
		newprofile.displayname = dn;
		text_changed = TRUE;
	}
	if (g_strcmp0(ds, p->orig_desc)) {
		newprofile.description = ds;
		text_changed = TRUE;
	}
	if (text_changed)
		YAMPUpdateUserProfile(mainsock, &newprofile, NULL);

	if (p->new_pfp_path)
		StartPfpUpload(p->new_pfp_path);

	g_free(p->orig_dpyname);
	p->orig_dpyname = dn;
	g_free(p->orig_desc);
	p->orig_desc = ds;
	g_clear_pointer(&p->new_pfp_path, g_free);

	ProfilePageRefresh(p);
}

void BuildUserFields(GtkWidget* suvbox, YampUser usr,
					 ProfilePagePayload* payload) {
	GtkWidget* overlay = gtk_overlay_new();

	GtkWidget* base = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	GtkWidget* banner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(banner, "banner");
	gtk_widget_set_size_request(banner, -1, 100);
	gtk_widget_set_hexpand(banner, TRUE);
	gtk_box_append(GTK_BOX(base), banner);

	GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(spacer, -1, 40);
	gtk_box_append(GTK_BOX(base), spacer);

	gtk_overlay_set_child(GTK_OVERLAY(overlay), base);

	GtkWidget* pfp;
	if (usr.pfp && strlen(usr.pfp)) {
		pfp = gtk_image_new();
		char* url = ProcessAttachmentURL(usr.pfp);
		LoadAvatarAsync(pfp, url, NULL, 80);
		free(url);
	} else {
		char pfppath[64];
		snprintf(pfppath, sizeof(pfppath),
				 "/org/yampen/assets/pfps/default%d.png",
				 GetDefaultPfp(usr.id));
		pfp = gtk_image_new_from_resource(pfppath);
	}
	gtk_image_set_pixel_size(GTK_IMAGE(pfp), 80);
	gtk_widget_add_css_class(pfp, "avatar");
	gtk_widget_set_overflow(pfp, GTK_OVERFLOW_HIDDEN);
	gtk_widget_set_halign(pfp, GTK_ALIGN_START);
	gtk_widget_set_valign(pfp, GTK_ALIGN_END);
	gtk_widget_set_margin_start(pfp, 16);
	gtk_widget_set_cursor_from_name(pfp, "pointer");
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), pfp);
	payload->pfp = pfp;

	GtkGesture* click = gtk_gesture_click_new();
	g_signal_connect(click, "released", G_CALLBACK(OnPfpClicked), payload);
	gtk_widget_add_controller(pfp, GTK_EVENT_CONTROLLER(click));

	GtkWidget* infoarea = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_hexpand(infoarea, 1);
	gtk_widget_set_margin_start(infoarea, 16);
	gtk_widget_set_margin_end(infoarea, 16);
	gtk_widget_set_halign(infoarea, GTK_ALIGN_FILL);

	GtkWidget* dpyname = gtk_entry_new();
	gtk_widget_set_hexpand(dpyname, 1);
	gtk_entry_set_has_frame(GTK_ENTRY(dpyname), FALSE);
	gtk_widget_add_css_class(dpyname, "h2");
	gtk_widget_set_halign(dpyname, GTK_ALIGN_FILL);

	gboolean has_dpy = usr.displayname && *usr.displayname;
	if (usr.username)
		gtk_entry_set_placeholder_text(GTK_ENTRY(dpyname), usr.username);
	if (has_dpy)
		gtk_editable_set_text(GTK_EDITABLE(dpyname), usr.displayname);
	else if (usr.username)
		gtk_editable_set_text(GTK_EDITABLE(dpyname), usr.username);
	gtk_box_append(GTK_BOX(infoarea), dpyname);
	payload->dpyname = dpyname;

	if (has_dpy) {
		GtkWidget* usrname = gtk_label_new(usr.username);
		gtk_label_set_xalign(GTK_LABEL(usrname), 0.0f);
		gtk_box_append(GTK_BOX(infoarea), usrname);
	}

	gtk_box_append(GTK_BOX(suvbox), overlay);
	gtk_box_append(GTK_BOX(suvbox), infoarea);
}

void BuildProfilePage(GtkWidget* page) {
	GtkWidget* contentbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_vexpand(contentbox, 1);
	gtk_widget_set_halign(contentbox, GTK_ALIGN_START);
	gtk_widget_set_size_request(contentbox, 300, -1);
	GtkWidget* btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(btnbox, GTK_ALIGN_END);

	gtk_widget_set_margin_top(page, 10);
	gtk_widget_set_margin_bottom(page, 10);
	gtk_widget_set_margin_start(page, 10);
	gtk_widget_set_margin_end(page, 10);

	ProfilePagePayload* payload = calloc(1, sizeof(ProfilePagePayload));

	YampUser* cur = GetUserObject(curUserID);
	YampUser usr = {0};
	if (cur)
		usr = *cur;

	payload->username = g_strdup(usr.username);
	payload->orig_dpyname = NormalizeText(usr.displayname);
	payload->orig_desc = NormalizeText(usr.description);

	GtkWidget* title = gtk_label_new(NULL);
	gtk_label_set_markup(
		GTK_LABEL(title),
		"<span size=\"large\" weight=\"bold\">Your Profile</span>");
	gtk_box_append(GTK_BOX(contentbox), title);

	BuildUserFields(contentbox, usr, payload);

	GtkWidget* desclbl = gtk_label_new("Description");
	gtk_widget_set_hexpand(desclbl, 1);
	gtk_widget_set_margin_top(desclbl, 12);
	gtk_widget_set_margin_start(desclbl, 16);
	gtk_label_set_xalign(GTK_LABEL(desclbl), 0.0f);
	gtk_box_append(GTK_BOX(contentbox), desclbl);

	GtkWidget* descscroll = gtk_scrolled_window_new();
	gtk_widget_set_margin_start(descscroll, 16);
	gtk_widget_set_margin_end(descscroll, 16);
	gtk_widget_set_size_request(descscroll, -1, 120);
	GtkWidget* descentry = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(descentry), GTK_WRAP_WORD_CHAR);
	gtk_widget_set_hexpand(descentry, 1);
	gtk_widget_set_vexpand(descentry, 1);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(descscroll), descentry);
	gtk_box_append(GTK_BOX(contentbox), descscroll);
	payload->desc = descentry;

	if (payload->orig_desc) {
		gtk_text_buffer_set_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(descentry)),
			payload->orig_desc, -1);
	}

	gtk_box_append(GTK_BOX(page), contentbox);

	GtkWidget* applybtn = gtk_button_new_with_label("Apply");
	gtk_widget_set_sensitive(applybtn, FALSE);
	g_signal_connect(applybtn, "clicked", G_CALLBACK(ProfilePageApply),
					 payload);
	gtk_box_append(GTK_BOX(btnbox), applybtn);
	gtk_box_append(GTK_BOX(page), btnbox);
	payload->applybtn = applybtn;

	g_signal_connect(payload->dpyname, "changed",
					 G_CALLBACK(ProfilePageChanged), payload);
	g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(descentry)),
					 "changed", G_CALLBACK(ProfilePageChanged), payload);

	g_signal_connect(page, "destroy", G_CALLBACK(ProfilePageFree), payload);
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