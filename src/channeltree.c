#include <gtk/gtk.h>
#include <yamp.h>
#include <channeltree.h>
#include <imwnd.h>
#include <globals.h>
#include <imwnd_parts.h>
#include <memberslist.h>
#include <avatar.h>
#include <helpers.h>
#include <hashtables.h>

GtkWidget* channeltree_root = NULL; // the box inside the sidebar scroller
char* channeltree_space = NULL;		// space that tree was built for


static void ChannelMenuCtxFree(gpointer data, GClosure* closure) {
	ChannelMenuCtx* ctx = data;
	if (!ctx)
		return;
	g_free(ctx->space);
	g_free(ctx->category);
	g_free(ctx);
}

void ChannelCreateBtnCallback(GtkButton* btn, gpointer dat) {
	ChannelCreateBtnCtx* createctx = dat;
	YAMPCreateChannel(mainsock, createctx->menuctx.space,
					  gtk_entry_buffer_get_text(gtk_entry_get_buffer(
						  GTK_ENTRY(createctx->nameentry))),
					  -1, YAMP_SPACE_TEXT, createctx->menuctx.category, NULL);
	gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn))));
	free(dat);
}
void CategoryCreateBtnCallback(GtkButton* btn, gpointer dat) {
	ChannelCreateBtnCtx* createctx = dat;
	YAMPCreateChannel(mainsock, createctx->menuctx.space,
					  gtk_entry_buffer_get_text(gtk_entry_get_buffer(
						  GTK_ENTRY(createctx->nameentry))),
					  -1, YAMP_SPACE_CATEGORY, createctx->menuctx.category,
					  NULL);
	gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn))));
	free(dat);
}
void ChannelCancelBtnCallback(GtkButton* btn, gpointer dat) {
	gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn))));
}
static void OnCreateChannel(GSimpleAction* action, GVariant* param,
							gpointer user_data) {
	GtkWidget* dialog = gtk_application_window_new(global_app);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 400);
	GtkWidget* mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget* titlelbl = gtk_label_new("Create a channel");
	gtk_box_append(GTK_BOX(mainbox), titlelbl);
	gtk_window_set_child(GTK_WINDOW(dialog), mainbox);
	gtk_window_present(GTK_WINDOW(dialog));
	GtkWidget* contentbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_vexpand(contentbox, 1);
	GtkWidget* namefieldlabel = gtk_label_new("Channel name");
	gtk_box_append(GTK_BOX(contentbox), namefieldlabel);
	GtkWidget* namefield = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(namefield), "my-channel");
	gtk_box_append(GTK_BOX(contentbox), namefield);
	gtk_box_append(GTK_BOX(mainbox), contentbox);
	GtkWidget* typefieldlbl = gtk_label_new("Channel type");
	gtk_box_append(GTK_BOX(contentbox), typefieldlbl);
	GtkWidget* textchoice = gtk_check_button_new_with_label("Text Channel");
	gtk_box_append(GTK_BOX(contentbox), textchoice);
	GtkWidget* btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget* cancelbtn = gtk_button_new_with_label("Cancel");
	g_signal_connect(cancelbtn, "clicked", G_CALLBACK(ChannelCancelBtnCallback),
					 NULL);
	GtkWidget* nextbtn = gtk_button_new_with_label("Create");
	ChannelCreateBtnCtx* createctx = malloc(sizeof(ChannelCreateBtnCtx));
	createctx->nameentry = namefield;
	createctx->menuctx = *(ChannelMenuCtx*)user_data;
	g_signal_connect(nextbtn, "clicked", G_CALLBACK(ChannelCreateBtnCallback),
					 createctx);
	gtk_widget_set_halign(btnbox, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(btnbox), cancelbtn);
	gtk_box_append(GTK_BOX(btnbox), nextbtn);
	gtk_box_append(GTK_BOX(mainbox), btnbox);
	free(user_data);
}
static void OnCreateCategory(GSimpleAction* action, GVariant* param,
							 gpointer user_data) {
	GtkWidget* dialog = gtk_application_window_new(global_app);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 400);
	GtkWidget* mainbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget* titlelbl = gtk_label_new("Create a category");
	gtk_box_append(GTK_BOX(mainbox), titlelbl);
	gtk_window_set_child(GTK_WINDOW(dialog), mainbox);
	gtk_window_present(GTK_WINDOW(dialog));
	GtkWidget* contentbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_vexpand(contentbox, 1);
	GtkWidget* namefieldlabel = gtk_label_new("Category name");
	gtk_box_append(GTK_BOX(contentbox), namefieldlabel);
	GtkWidget* namefield = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(namefield), "my-channel");
	gtk_box_append(GTK_BOX(contentbox), namefield);
	gtk_box_append(GTK_BOX(mainbox), contentbox);
	GtkWidget* btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget* cancelbtn = gtk_button_new_with_label("Cancel");
	g_signal_connect(cancelbtn, "clicked", G_CALLBACK(ChannelCancelBtnCallback),
					 NULL);
	GtkWidget* nextbtn = gtk_button_new_with_label("Create");
	ChannelCreateBtnCtx* createctx = malloc(sizeof(ChannelCreateBtnCtx));
	createctx->nameentry = namefield;
	createctx->menuctx = *(ChannelMenuCtx*)user_data;
	g_signal_connect(nextbtn, "clicked", G_CALLBACK(CategoryCreateBtnCallback),
					 createctx);
	gtk_widget_set_halign(btnbox, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(btnbox), cancelbtn);
	gtk_box_append(GTK_BOX(btnbox), nextbtn);
	gtk_box_append(GTK_BOX(mainbox), btnbox);
	free(user_data);
}
GtkWidget* MakeChannelContextMenu(GtkWidget* attach_widget,
								  ChannelMenuCtx* ctx) {
	GSimpleActionGroup* group = g_simple_action_group_new();
	GSimpleAction* create_action = g_simple_action_new("create-channel", NULL);
	g_signal_connect(create_action, "activate", G_CALLBACK(OnCreateChannel),
					 ctx);
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(create_action));

	GSimpleAction* create_cat_action =
		g_simple_action_new("create-category", NULL);
	g_signal_connect(create_cat_action, "activate",
					 G_CALLBACK(OnCreateCategory), ctx);
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(create_cat_action));

	gtk_widget_insert_action_group(attach_widget, "channelmenu",
								   G_ACTION_GROUP(group));
	g_object_unref(group);

	GMenu* menu = g_menu_new();
	g_menu_append(menu, "Create Channel", "channelmenu.create-channel");
	GMenu* catmenu = g_menu_new();
	g_menu_append(menu, "Create Category", "channelmenu.create-category");

	GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	gtk_widget_set_parent(popover, attach_widget);
	gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);

	g_object_unref(menu);
	return popover;
}
void ShowChannelContextMenu(GtkWidget* attach_widget, const char* category,
							double x, double y) {
	GtkWidget* old =
		g_object_get_data(G_OBJECT(attach_widget), "channel-context-popover");
	if (old) {
		gtk_widget_unparent(old);
	}

	ChannelMenuCtx* ctx = malloc(sizeof(ChannelMenuCtx));
	ctx->space = curSpace;
	ctx->category = category ? strdup(category) : NULL;

	GtkWidget* popover = MakeChannelContextMenu(attach_widget, ctx);
	g_object_set_data(G_OBJECT(attach_widget), "channel-context-popover",
					  popover);

	GdkRectangle rect = {(int)x, (int)y, 1, 1};
	gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
	gtk_widget_set_halign(GTK_WIDGET(popover), GTK_ALIGN_START);
	gtk_popover_popup(GTK_POPOVER(popover));
}
void OnChannelSidebarRightClick(GtkGestureClick* gesture, int n_press, double x,
								double y, gpointer user_data) {
	GtkWidget* widget =
		gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	ShowChannelContextMenu(widget, NULL, x, y);
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}
void OnCategoryHeaderRightClick(GtkGestureClick* gesture, int n_press, double x,
								double y, gpointer user_data) {
	GtkWidget* widget =
		gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	char* categoryname = g_object_get_data(G_OBJECT(widget), "category-name");
	ShowChannelContextMenu(widget, categoryname, x, y);
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}
void OnCategoryToggle(GtkGestureClick* gesture, int n_press, double x, double y,
					  gpointer user_data) {
	GtkWidget* content_box = GTK_WIDGET(user_data);
	GtkWidget* toggle_label =
		g_object_get_data(G_OBJECT(content_box), "toggle-label");
	gboolean visible = gtk_widget_get_visible(content_box);
	gtk_widget_set_visible(content_box, !visible);
	gtk_label_set_text(GTK_LABEL(toggle_label), visible ? "+" : "-");
}
GtkWidget* BuildChannelEntry(YampChannel* ch, int depth) {
	if (ch->type == YAMP_SPACE_CATEGORY) {
		GtkWidget* wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		g_object_set_data_full(G_OBJECT(wrapper), "channel-id",
							   g_strdup(ch->id), g_free);

		GtkWidget* header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		gtk_widget_set_margin_start(header_row, depth * 12);
		gtk_widget_set_cursor_from_name(header_row, "pointer");
		gtk_widget_add_css_class(header_row, "channel-category");
		GtkWidget* toggle_label = gtk_label_new("-");
		gtk_widget_set_valign(toggle_label, GTK_ALIGN_CENTER);
		gtk_widget_add_css_class(toggle_label, "channel-category");
		gtk_widget_set_cursor_from_name(toggle_label, "pointer");
		GtkWidget* header = gtk_label_new(ch->name);
		gtk_widget_set_halign(header, GTK_ALIGN_START);
		gtk_widget_add_css_class(header, "channel-category");
		gtk_box_append(GTK_BOX(header_row), toggle_label);
		gtk_box_append(GTK_BOX(header_row), header);
		gtk_box_append(GTK_BOX(wrapper), header_row);

		GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_box_append(GTK_BOX(wrapper), content_box);
		g_object_set_data(G_OBJECT(content_box), "toggle-label", toggle_label);
		g_object_set_data_full(G_OBJECT(content_box), "category-id",
							   g_strdup(ch->id), g_free);
		g_object_set_data(G_OBJECT(content_box), "depth",
						  GINT_TO_POINTER(depth + 1));

		GtkGesture* toggleclick = gtk_gesture_click_new();
		g_signal_connect(toggleclick, "pressed", G_CALLBACK(OnCategoryToggle),
						 content_box);
		gtk_widget_add_controller(header_row,
								  GTK_EVENT_CONTROLLER(toggleclick));

		g_object_set_data_full(G_OBJECT(header_row), "category-name",
							   g_strdup(ch->id), g_free);
		GtkGesture* catclick = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(catclick),
									  GDK_BUTTON_SECONDARY);
		g_signal_connect(catclick, "pressed",
						 G_CALLBACK(OnCategoryHeaderRightClick), NULL);
		gtk_widget_add_controller(header_row, GTK_EVENT_CONTROLLER(catclick));

		for (int i = 0; i < ch->nchildren; i++)
			gtk_box_append(GTK_BOX(content_box),
						   BuildChannelEntry(ch->children[i], depth + 1));
		return wrapper;
	}

	GtkWidget* listbox = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox),
									GTK_SELECTION_SINGLE);
	gtk_widget_set_margin_start(listbox, depth * 16);
	g_signal_connect(listbox, "row-selected", G_CALLBACK(OnChannelSelected),
					 NULL);
	g_object_set_data_full(G_OBJECT(listbox), "channel-id", g_strdup(ch->id),
						   g_free);

	GtkWidget* lbr = gtk_list_box_row_new();
	gtk_widget_add_css_class(lbr, "channel-row");
	GtkWidget* label = gtk_label_new(ch->name);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_margin_top(label, 5);
	gtk_widget_set_margin_bottom(label, 5);
	GtkWidget* contentbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_append(GTK_BOX(contentbox),
				   gtk_image_new_from_icon_name("text-channel-symbolic"));
	gtk_box_append(GTK_BOX(contentbox), label);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(lbr), contentbox);
	gtk_list_box_append(GTK_LIST_BOX(listbox), lbr);
	g_object_set_data(G_OBJECT(lbr), "channel", DupChannelObject(ch));
	return listbox;
}
int DeleteChannelEntry(GtkWidget* parent_box, char* channel_id) {
	GtkWidget* wdg =
		RecursiveFindWidgetByData(parent_box, "channel-id", channel_id);
	if (wdg) {
		gtk_list_box_remove(GTK_LIST_BOX(gtk_widget_get_parent(wdg)), wdg);
		return 1;
	}
	return 0;
}

void BuildChannelTree(GtkWidget* parent_box, YampChannel** channels,
					  int nchannels, int depth) {
	if (!channels)
		return;
	for (int i = 0; i < nchannels; i++)
		gtk_box_append(GTK_BOX(parent_box),
					   BuildChannelEntry(channels[i], depth));
}




// DMs List

typedef struct {
	char* id;
	GtkWidget* row;
} GCLeaveCtx;

static void GCLeaveCtxFree(gpointer data, GClosure* closure) {
	GCLeaveCtx* ctx = data;
	free(ctx->id);
	free(ctx);
}

static gboolean GCLeaveRemoveRowIdle(gpointer data) {
	GCLeaveCtx* ctx = data;
	GtkWidget* row = ctx->row;
	gboolean was_current =
		currentChannel && strcmp(currentChannel->id, ctx->id) == 0;

	if (gtk_widget_get_parent(row) == dmlist)
		gtk_list_box_remove(GTK_LIST_BOX(dmlist), row);
	g_object_unref(row);

	if (was_current) {
		YAMPLeaveChannel(mainsock, currentChannel->id, NULL);
		currentChannel = NULL;
		TypingClear();
		gtk_list_box_remove_all(GTK_LIST_BOX(chatarea));
		GtkWidget* titlewdg;
		while ((titlewdg = gtk_widget_get_first_child(chattitle)) != NULL)
			gtk_widget_unparent(titlewdg);
		GtkWidget* w;
		while ((w = gtk_widget_get_first_child(suvbox)) != NULL)
			gtk_widget_unparent(w);
		MembersListShown = 0;
		gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "friends");
	}
	free(ctx->id);
	free(ctx);
	return G_SOURCE_REMOVE;
}

static void OnLeaveGCAction(GSimpleAction* action, GVariant* param,
							gpointer data) {
	GCLeaveCtx* src = data;
	YAMPLeaveGC(mainsock, src->id, NULL);
}

static void OnGCRowRightClick(GtkGestureClick* gesture, int n_press, double x,
							  double y, gpointer popover) {
	GdkRectangle rect = {(int)x, (int)y, 1, 1};
	gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
	gtk_popover_popup(GTK_POPOVER(popover));
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

void AttachGCContextMenu(GtkWidget* row, const char* gc_id) {
	GCLeaveCtx* ctx = malloc(sizeof(*ctx));
	ctx->id = strdup(gc_id);
	ctx->row = row;

	GSimpleActionGroup* group = g_simple_action_group_new();
	GSimpleAction* leave = g_simple_action_new("leave", NULL);
	g_signal_connect_data(leave, "activate", G_CALLBACK(OnLeaveGCAction), ctx,
						  GCLeaveCtxFree, 0);
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(leave));
	g_object_unref(leave);
	gtk_widget_insert_action_group(row, "gcmenu", G_ACTION_GROUP(group));
	g_object_unref(group);

	GMenu* menu = g_menu_new();
	g_menu_append(menu, "Leave group", "gcmenu.leave");
	GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	g_object_unref(menu);
	gtk_widget_set_parent(popover, row);
	gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
	gtk_widget_set_halign(popover, GTK_ALIGN_START);
	// popover must be unparented before the row goes away
	g_signal_connect_swapped(row, "destroy", G_CALLBACK(gtk_widget_unparent),
							 popover);

	GtkGesture* click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),
								  GDK_BUTTON_SECONDARY);
	g_signal_connect(click, "pressed", G_CALLBACK(OnGCRowRightClick), popover);
	gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));
}

int InsertConvIntoList(YampChannel* conv) {
	if (conv->type == YAMP_DM) {
		YampUser** members = conv->people;
		YampUser* usr; // the other user
		if (strcmp(members[0]->id, curUserID) == 0) {
			usr = members[1];
		} else {
			usr = members[0];
		}
		char* dispname = usr->displayname ? usr->displayname : usr->username;
		YampUser* heapusr = DupUserObject(usr);
		InsertUserObject(heapusr->id, heapusr);
		GtkWidget* ItemBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		GtkWidget* LBRow = gtk_list_box_row_new();
		GtkWidget* LBRowLabel = gtk_label_new(dispname);
		gtk_label_set_ellipsize(GTK_LABEL(LBRowLabel), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(LBRowLabel), 1);
		gtk_widget_set_hexpand(LBRowLabel, TRUE);
		gtk_label_set_xalign(GTK_LABEL(LBRowLabel), 0.0f);
		GtkWidget* Pfp;
		if (usr->pfp && strlen(usr->pfp)) {
			Pfp = gtk_image_new();
			gtk_widget_add_css_class(Pfp, "avatar");
			gtk_widget_set_overflow(Pfp, GTK_OVERFLOW_HIDDEN);

			char* fetchurl = ProcessAttachmentURL(usr->pfp);
			char* pfp_path = AvatarCachePath(fetchurl);
			LoadAvatarAsync(Pfp, fetchurl, pfp_path, 32);
			free(fetchurl);
			InsertPfpPath(usr->id, pfp_path);
		} else {
			char pfppath[37];
			sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
					GetDefaultPfp(usr->id));
			Pfp = gtk_image_new_from_resource(pfppath);
			gtk_widget_add_css_class(Pfp, "avatar");
			gtk_widget_set_overflow(Pfp, GTK_OVERFLOW_HIDDEN);
		}
		char* statusClr = StatusToColor(usr->status->status);
		GtkWidget* statusframe = WrapInStatusFrame(Pfp, 32, statusClr);
		g_object_set_data_full(G_OBJECT(statusframe), "user-id",
							   g_strdup(usr->id), g_free);
		g_object_set_data(G_OBJECT(LBRow), "id", heapusr->id);
		g_object_set_data_full(G_OBJECT(LBRow), "channel",
							   DupChannelObject(conv),
							   (GDestroyNotify)FreeChannelObject);
		g_object_set_data_full(G_OBJECT(LBRow), "dpyname", g_strdup(dispname),
							   g_free);
		g_object_set_data(G_OBJECT(LBRow), "type", (void*)conv->type);
		gtk_box_append(GTK_BOX(ItemBox), statusframe);
		gtk_box_append(GTK_BOX(ItemBox), LBRowLabel);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(LBRow), ItemBox);
		gtk_list_box_append(GTK_LIST_BOX(dmlist), LBRow);
	} else if (conv->type == YAMP_GC) {
		YampUser** members = conv->people;
		char* dispname = conv->name ? dup0(conv->name)
									: JoinUsernames(members, conv->npeople);
		GtkWidget* ItemBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		GtkWidget* LBRow = gtk_list_box_row_new();
		GtkWidget* LBRowLabel = gtk_label_new(dispname);
		g_object_set_data(G_OBJECT(LBRowLabel), "tag", "name");
		gtk_label_set_ellipsize(GTK_LABEL(LBRowLabel), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(LBRowLabel), 1);
		gtk_widget_set_hexpand(LBRowLabel, TRUE);
		gtk_label_set_xalign(GTK_LABEL(LBRowLabel), 0.0f);
		GtkWidget* Pfp;
		if (conv->icon) {
			char pfppath[37];
			sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
					GetDefaultPfp(conv->id));
			Pfp = gtk_image_new_from_resource(pfppath);
		} else {
			char pfppath[37];
			sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
					GetDefaultPfp(conv->id));
			Pfp = gtk_image_new_from_resource(pfppath);
		}
		g_object_set_data(G_OBJECT(Pfp), "tag", "icon");

		GtkCssProvider* provider = gtk_css_provider_new();
		char style[60];
		sprintf(style, "image { -gtk-icon-size: 32px; }");
		gtk_css_provider_load_from_string(provider, style);
		gtk_style_context_add_provider(gtk_widget_get_style_context(Pfp),
									   GTK_STYLE_PROVIDER(provider),
									   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_set_data_full(G_OBJECT(LBRow), "channel",
							   DupChannelObject(conv),
							   (GDestroyNotify)FreeChannelObject);
		g_object_set_data(G_OBJECT(LBRow), "id", strdup(conv->id));
		g_object_set_data(G_OBJECT(LBRow), "type", (void*)conv->type);
		g_object_set_data_full(G_OBJECT(LBRow), "members",
							   MemberSetCopy(conv->people, conv->npeople),
							   MemberSetFree);
		g_object_set_data(G_OBJECT(LBRow), "dpyname", dispname);
		gtk_box_append(GTK_BOX(ItemBox), Pfp);
		gtk_box_append(GTK_BOX(ItemBox), LBRowLabel);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(LBRow), ItemBox);
		gtk_list_box_append(GTK_LIST_BOX(dmlist), LBRow);
		AttachGCContextMenu(LBRow, conv->id);
		free(dispname);
	}
	return 1;
}
int ModifyConvInList(YampChannel* ch) {
	if (ch->type == YAMP_GC) {
		printf("GC ID %s\n", ch->name);
		GtkWidget* row =
			RecursiveFindWidgetByData(ChannelSidebar, "id", ch->id);
		if (!row) {
			return 0;
		}
		YampChannel* internal_ch = DupChannelObject(ch);

		g_object_set_data_full(G_OBJECT(row), "channel", internal_ch,
							   (GDestroyNotify)FreeChannelObject);
		GtkWidget* name_label = RecursiveFindWidgetByData(row, "tag", "name");
		if (!name_label) {
			return 0;
		}
		char* dispname = ch->name ? strdup(ch->name)
								  : JoinUsernames(ch->people, ch->npeople);

		gtk_label_set_text(GTK_LABEL(name_label), dispname);
		free(dispname);
		return 1;
	}
	return 0;
}
int DeleteConvFromList(char* id) {
	GtkWidget* wdg = RecursiveFindWidgetByData(ChannelSidebar, "id", id);
	if (strcmp(currentChannel->id, id) == 0) {
		currentChannel = NULL;
	}
	if (wdg) {
		gtk_list_box_remove(GTK_LIST_BOX(gtk_widget_get_parent(wdg)), wdg);
	}
	return 0;
}