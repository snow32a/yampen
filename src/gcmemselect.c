#include <globals.h>
#include <yamp.h>
#include <gtk/gtk.h>
#include <hashtables.h>
#include <avatar.h>
#include <imwnd.h>
typedef struct {
	GtkWidget* wnd;
	GtkWidget* memberlist;
	void (*callback)(YampUser** users, int nusers);
} GCInitSelectorCtx;

void OnGCInitMemberSelectorConfirm(GtkButton* button, gpointer data) {
	GCInitSelectorCtx* ctx = data;

	int total = 0;
	while (gtk_list_box_get_row_at_index(GTK_LIST_BOX(ctx->memberlist), total))
		total++;

	YampUser** selected = g_new0(YampUser*, total ? total : 1);
	int nselected = 0;

	for (int i = 0; i < total; i++) {
		GtkListBoxRow* row =
			gtk_list_box_get_row_at_index(GTK_LIST_BOX(ctx->memberlist), i);
		GtkWidget* check = gtk_list_box_row_get_child(row);
		if (gtk_check_button_get_active(GTK_CHECK_BUTTON(check))) {
			YampUser* user = g_object_get_data(G_OBJECT(row), "user");
			selected[nselected++] = user;
		}
	}

	if (nselected == 0) {
		g_free(selected);
		return;
	}

	if (ctx->callback)
		ctx->callback(selected, nselected);
	g_free(selected);
	gtk_window_destroy(GTK_WINDOW(ctx->wnd));
}

void SpawnGCInitMemberSelector(YampUser** content, int ncontent,
							   void (*callback)(YampUser** users, int nusers)) {
	GtkWidget* wnd = gtk_application_window_new(global_app);
	gtk_window_set_default_size(GTK_WINDOW(wnd), 300, 400);

	GCInitSelectorCtx* ctx = g_new0(GCInitSelectorCtx, 1);
	ctx->wnd = wnd;
	ctx->callback = callback;
	g_signal_connect_swapped(wnd, "destroy", G_CALLBACK(g_free), ctx);

	GtkWidget* mainvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_margin_start(mainvbox, 12);
	gtk_widget_set_margin_end(mainvbox, 12);
	gtk_widget_set_margin_bottom(mainvbox, 12);
	GtkWidget* titlebar = gtk_label_new(NULL);
	gtk_label_set_markup(
		GTK_LABEL(titlebar),
		"<span size=\"large\" weight=\"bold\">Select members</span>");


	gtk_widget_set_size_request(titlebar, -1, 60);
    
	gtk_box_append(GTK_BOX(mainvbox), titlebar);

	GtkWidget* memberlist = gtk_list_box_new();
    gtk_widget_set_vexpand(GTK_WIDGET(memberlist), TRUE);
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(memberlist),
									GTK_SELECTION_NONE);
	ctx->memberlist = memberlist;

	for (int i = 0; i < ncontent; i++) {
		GtkWidget* userrow = gtk_list_box_row_new();
		GtkWidget* check = gtk_check_button_new();
		GtkWidget* btncontent = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		gtk_check_button_set_child(GTK_CHECK_BUTTON(check), btncontent);
		GtkWidget* pfp;
		if (content[i]->pfp && strlen(content[i]->pfp)) {
			char* url = ProcessAttachmentURL(content[i]->pfp);
			char* pfp_path = AvatarCachePath(url);
			pfp = gtk_image_new();
			LoadAvatarAsync(pfp, url, pfp_path, 36);
			free(url);
			InsertPfpPath(content[i]->id, pfp_path);
		} else {
			char pfppath[37];
			sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
					GetDefaultPfp(content[i]->id));
			pfp = gtk_image_new_from_resource(pfppath);
		}
        gtk_image_set_pixel_size(GTK_IMAGE(pfp), 36);
        gtk_widget_set_overflow(pfp, GTK_OVERFLOW_HIDDEN);
        gtk_widget_add_css_class(pfp, "avatar");

		GtkWidget* name_lbl =
			gtk_label_new(content[i]->displayname ? content[i]->displayname
												  : content[i]->username);
		gtk_box_append(GTK_BOX(btncontent), pfp);
		gtk_box_append(GTK_BOX(btncontent), name_lbl);
		g_object_set_data(G_OBJECT(userrow), "user", content[i]);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(userrow), check);
		gtk_list_box_append(GTK_LIST_BOX(memberlist), userrow);
	}

	GtkWidget* scroller = gtk_scrolled_window_new();
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), memberlist);
	gtk_box_append(GTK_BOX(mainvbox), scroller);

	GtkWidget* buttonbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(buttonbox, GTK_ALIGN_END);
	gtk_widget_set_margin_top(buttonbox, 6);

	GtkWidget* confirmbtn = gtk_button_new_with_label("Done");
	g_signal_connect(confirmbtn, "clicked",
					 G_CALLBACK(OnGCInitMemberSelectorConfirm), ctx);
	gtk_box_append(GTK_BOX(buttonbox), confirmbtn);

	gtk_box_append(GTK_BOX(mainvbox), buttonbox);
	gtk_window_set_child(GTK_WINDOW(wnd), mainvbox);
	gtk_window_present(GTK_WINDOW(wnd));
}