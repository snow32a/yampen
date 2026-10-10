#include <stdio.h>
#include <gtk/gtk.h>
#include "imwnd.h"
#include "gio/gio.h"
#include "gio/gmenu.h"
#include "glib-object.h"
#include "glib.h"
#include "login.h"
#include "chatwnd.h"
#include "pango/pango-attributes.h"
#include "settings.h"
#include "glibconfig.h"
#include "gtk/gtkshortcut.h"
#include "protocol/yamp.h"
#include "globals.h"
#include "hashtables.h"
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include "monocypher.h"
#include "insertspace.h"
#include "notification.h"
#include "usersettings.h"
#include "gcmemselect.h"

char* curUserID;
GtkWidget* main_window;
GtkWidget* ChannelSidebar;
GtkWidget* dmsbutton;
GtkWidget* GuildVBOX;
GtkWidget* GuildList;
GtkWidget* SelfPfp;
GtkWidget* chatscroll;
GtkWidget* chatarea;
GtkWidget* DisplayNameLabel;
GtkWidget* UsernameLabel;
GtkWidget* selectedspacebtn;
GtkWidget* stackpane;
GtkWidget* notifoverlay;
GtkWidget* dmlist;
GtkWidget* ListVBOX;
GtkWidget* ListTitle;
GtkWidget* suvbox;
GtkWidget* chattitle;
gboolean listmode = FALSE;
char* curSpace = NULL;
char* curSpaceDisplay = NULL;
CURL* curl;
char* currentChat;
char* pfp_dir;

static size_t file_write_cb(char* ptr, size_t size, size_t nmemb,
							void* userdata) {
	return fwrite(ptr, size, nmemb, (FILE*)userdata);
}

static gboolean onUIDisconnected(gpointer none) {
	GtkAlertDialog* alert = gtk_alert_dialog_new("Server disconnected!");
	gtk_alert_dialog_show(alert, NULL);
	DisplayLoginDialog(global_app);
	return G_SOURCE_REMOVE;
}

typedef struct {
	GtkWidget* EntryArea;
	GtkWidget* ChatView;
} EKeyPayload;

YampChannel* cached_convs = NULL;
int ncached_convs = 0;
YampUser* cached_friends = NULL;
int ncached_friends = 0;

static gboolean EntryKeyHandler(GtkEventControllerKey* controller, guint keyval,
								guint keycode, GdkModifierType state,
								gpointer user_data) {
	if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) &&
		!(state & GDK_SHIFT_MASK)) {

		EKeyPayload* dat = (EKeyPayload*)user_data;
		GtkTextBuffer* buf =
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea));

		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter(buf, &start);
		gtk_text_buffer_get_end_iter(buf, &end);

		char* content = gtk_text_buffer_get_text(buf, &start, &end, TRUE);

		char* first = content;
		while (*first && g_ascii_isspace((guchar)*first))
			first++;

		char* last = content + strlen(content);
		while (last > first && g_ascii_isspace((guchar)last[-1]))
			last--;

		if (first == last) {
			g_free(content);
			return GDK_EVENT_STOP;
		}

		*last = '\0';

		YAMPSendIM(mainsock, currentChat, first);

		gtk_text_buffer_set_text(buf, "", 0);

		g_free(content);
		return GDK_EVENT_STOP;
	}

	return FALSE;
}

void onYAMPDisconnected() { g_idle_add(onUIDisconnected, 0); }
void BuildUserProfile(YampUser usr) {
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

	} else {
		char pfppath[38];
		sprintf(pfppath, "/org/yampen/assets/pfps/default%d.png",
				GetDefaultPfp(usr.id));
		pfp = gtk_image_new_from_resource(pfppath);
	}
	gtk_image_set_pixel_size(GTK_IMAGE(pfp), 80);
	gtk_widget_add_css_class(pfp, "avatar");
	gtk_widget_set_overflow(pfp, GTK_OVERFLOW_HIDDEN);
	gtk_widget_set_halign(pfp, GTK_ALIGN_START);
	gtk_widget_set_valign(pfp, GTK_ALIGN_END);
	gtk_widget_set_margin_start(pfp, 16);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), pfp);

	GtkWidget* infoarea = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_hexpand(infoarea, 1);
	gtk_widget_set_margin_start(infoarea, 16);
	gtk_widget_set_margin_end(infoarea, 16);
	gtk_widget_set_halign(infoarea, GTK_ALIGN_START);
	if (usr.displayname) {
		GtkWidget* dpyname = gtk_label_new(usr.displayname);
		gtk_widget_add_css_class(dpyname, "h2");
		gtk_box_append(GTK_BOX(infoarea), dpyname);
		GtkWidget* usrname = gtk_label_new(usr.username);
		gtk_label_set_xalign(GTK_LABEL(usrname), 0.0f);
		gtk_box_append(GTK_BOX(infoarea), usrname);
	} else {
		GtkWidget* usrname = gtk_label_new(usr.username);
		gtk_widget_add_css_class(usrname, "h2");
		gtk_box_append(GTK_BOX(infoarea), usrname);
	}
	gtk_box_append(GTK_BOX(suvbox), overlay);
	gtk_box_append(GTK_BOX(suvbox), infoarea);
}
void OnDMRowSelected(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
	gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "chat");
	if (!row) {
		return;
	}
	if (currentChat) {
		free(currentChat);
	}
	GtkWidget* wdg;
	while ((wdg = gtk_widget_get_first_child(suvbox)) != NULL)
		gtk_widget_unparent(wdg);
	GtkWidget* child = gtk_widget_get_next_sibling(
		gtk_widget_get_first_child(gtk_list_box_row_get_child(row)));
	int type = (int)g_object_get_data(G_OBJECT(child), "type");
	if (type == YAMP_DM) {
		BuildUserProfile(
			*GetUserObject(g_object_get_data(G_OBJECT(child), "id")));
		char* name = gtk_label_get_text(GTK_LABEL(child));
		currentChat = MakeDMChannel(curUserID, g_object_get_data(G_OBJECT(child), "id"));
		gtk_list_box_remove_all(GTK_LIST_BOX(chatarea));
		YAMPGetMessageHistory(mainsock, currentChat);
	} else if (type == YAMP_GC){
		char* name = gtk_label_get_text(GTK_LABEL(child));
		currentChat = MakeGCChannel(g_object_get_data(G_OBJECT(child), "id"));
		gtk_list_box_remove_all(GTK_LIST_BOX(chatarea));
		YAMPGetMessageHistory(mainsock, currentChat);
	}
}
void RecursiveDeselectChannelTree(GtkWidget* parentwdg,
								  GtkWidget* skiplistbox) {
	for (GtkWidget* wdg = gtk_widget_get_first_child(parentwdg); wdg;
		 wdg = gtk_widget_get_next_sibling(wdg)) {
		if (GTK_IS_LIST_BOX(wdg)) {
			if (wdg != skiplistbox) {
				gtk_list_box_unselect_all(GTK_LIST_BOX(wdg));
			}
		} else {
			RecursiveDeselectChannelTree(wdg, skiplistbox);
		}
	}
}
void OnChannelSelected(GtkListBox* box, GtkListBoxRow* row,
					   gpointer user_data) {
	gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "chat");
	if (!row) {
		return;
	}
	if (currentChat) {
		free(currentChat);
	}
	RecursiveDeselectChannelTree(ChannelSidebar, GTK_WIDGET(box));
	char* channelname =
		((YampChannel*)g_object_get_data(G_OBJECT(row), "channel"))->id;
	char* loc = malloc(1 + strlen(curSpace) + strlen(channelname) + 2);
	*loc = '^';
	strcpy(loc + 1, curSpace);
	*(loc + strlen(curSpace) + 1) = '#';
	strcpy(loc + strlen(curSpace) + 2, channelname);
	currentChat = loc;
	gtk_list_box_remove_all(GTK_LIST_BOX(chatarea));
	YAMPGetMessageHistory(mainsock, loc);
}
void SpaceRowCB(GtkButton* btn, gpointer user_data) {
	gtk_widget_remove_css_class(dmsbutton, "active");
	gtk_widget_remove_css_class(selectedspacebtn, "active");
	gtk_widget_add_css_class(GTK_WIDGET(btn), "active");
	selectedspacebtn = GTK_WIDGET(btn);
	listmode = FALSE;
	YampSpace* space = user_data;
	curSpace = space->id;

	YAMPListSpaceChannels(mainsock, curSpace);
}
int InsertSpace(YampSpace space) {
	printf("%s\n", space.id);
	GtkWidget* gldbtnrow = gtk_list_box_row_new();
	GtkWidget* spacebtn = gtk_button_new();
	YampSpace* heapspace = malloc(sizeof(YampSpace));
	*heapspace = space;
	strcpy(heapspace->id, space.id);
	g_signal_connect(spacebtn, "clicked", G_CALLBACK(SpaceRowCB), heapspace);
	InsertSpaceObject(strdup(space.id), heapspace);
	gtk_widget_set_size_request(spacebtn, 52, 52);
	gtk_widget_set_hexpand(spacebtn, FALSE);
	gtk_widget_set_vexpand(spacebtn, FALSE);
	GtkIconTheme* theme =
		gtk_icon_theme_get_for_display(gdk_display_get_default());
	// Worst case allocation size, if the text is literally jhas no fucking
	// spaces in the name
	char* dpyname = space.displayname;
	char* initials = malloc(strlen(dpyname) + 1);
	initials[0] = *dpyname;
	int initctr = 1;
	for (char* curChar = dpyname; *curChar; curChar++) {
		if ((*curChar == ' ') && (curChar[1]) && (curChar[1] != ' ')) {
			initials[initctr] = curChar[1];
			initctr++;
		}
	}
	initials[initctr] = '\0';
	GtkWidget* lbl = gtk_label_new(initials);
	free(initials); // gtk strdups it internally
	gtk_button_set_child(GTK_BUTTON(spacebtn), lbl);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(gldbtnrow), spacebtn);

	GtkCssProvider* dmsprovider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(
		dmsprovider, "button {"
					 "	color: #f25858;"
					 "  background: rgba(255,255,255,0.15);"
					 "  border-radius: 16px;"
					 "  padding: 0;"
					 "  min-width: 48px;"
					 "  min-height: 48px;"
					 "  border: none;"
					 "  transition: color 200ms ease, border-radius 200ms "
					 "ease, background 200ms ease;"
					 "}"
					 "button:hover {"
					 "	color: #404040;"
					 "  background: #f25858;"
					 "  border-radius: 12px;"
					 "}"
					 "button.active {"
					 "	color: #404040;"
					 "  background: #f25858;"
					 "  border-radius: 12px;"
					 "}");
	gtk_style_context_add_provider(gtk_widget_get_style_context(spacebtn),
								   GTK_STYLE_PROVIDER(dmsprovider),
								   GTK_STYLE_PROVIDER_PRIORITY_USER);


	gtk_list_box_append(GTK_LIST_BOX(GuildList), gldbtnrow);
	g_object_set_data(G_OBJECT(gldbtnrow), "name", space.name);
	g_object_set_data(G_OBJECT(gldbtnrow), "id", space.id);
}
int LoadSpaces(YampSpace* spaces, int nspaces) {
	for (int i = 0; i < nspaces; i++) {
		InsertSpace(spaces[i]);
	}
}
typedef struct {
	YampSpace* spaces;
	int nspaces;
} SpaceFetchPayload;
gboolean MainThreadSpaceCB(void* data) {
	SpaceFetchPayload* payload = data;
	LoadSpaces(payload->spaces, payload->nspaces);
	free(payload);
	return G_SOURCE_REMOVE;
}
void onYAMPSpacesFetched(YampSpace* spaces, int nspaces) {
	SpaceFetchPayload* payload = malloc(sizeof(SpaceFetchPayload));
	payload->spaces = spaces;
	payload->nspaces = nspaces;
	g_idle_add(MainThreadSpaceCB, payload);
}
gboolean MainThreadNewSpace(void* heapspace) {
	InsertSpace(*(YampSpace*)heapspace);
	free(heapspace);
	return G_SOURCE_REMOVE;
}
void onYAMPNewSpace(YampSpace space) {
	YampSpace* heapspace = malloc(sizeof(space));
	*heapspace = space;
	g_idle_add(MainThreadNewSpace, heapspace);
}

typedef struct {
	YampChannel* channels;
	int nchannels;
} ChannelFetchPayload;
typedef struct {
	char* space;
	char* category;
} ChannelMenuCtx;
typedef struct {
	ChannelMenuCtx menuctx;
	GtkWidget* nameentry;
} ChannelCreateBtnCtx;
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
					  -1, 0, createctx->menuctx.category);
	gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn))));
	free(dat);
}
void CategoryCreateBtnCallback(GtkButton* btn, gpointer dat) {
	ChannelCreateBtnCtx* createctx = dat;
	YAMPCreateChannel(mainsock, createctx->menuctx.space,
					  gtk_entry_buffer_get_text(gtk_entry_get_buffer(
						  GTK_ENTRY(createctx->nameentry))),
					  -1, 1, createctx->menuctx.category);
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

static GtkWidget* MakeChannelContextMenu(GtkWidget* attach_widget,
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
	g_menu_append(menu, "Create channel", "channelmenu.create-channel");
	GMenu* catmenu = g_menu_new();
	g_menu_append(menu, "Create category", "channelmenu.create-category");

	GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
	gtk_widget_set_parent(popover, attach_widget);
	gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);

	g_object_unref(menu);
	return popover;
}
static void ShowChannelContextMenu(GtkWidget* attach_widget,
								   const char* category, double x, double y) {
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
	gtk_popover_popup(GTK_POPOVER(popover));
}
static void OnChannelSidebarRightClick(GtkGestureClick* gesture, int n_press,
									   double x, double y, gpointer user_data) {
	GtkWidget* widget =
		gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	ShowChannelContextMenu(widget, NULL, x, y);
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}
static void OnCategoryHeaderRightClick(GtkGestureClick* gesture, int n_press,
									   double x, double y, gpointer user_data) {
	GtkWidget* widget =
		gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	char* categoryname = g_object_get_data(G_OBJECT(widget), "category-name");
	ShowChannelContextMenu(widget, categoryname, x, y);
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}
static void OnCategoryToggle(GtkGestureClick* gesture, int n_press, double x,
							 double y, gpointer user_data) {
	GtkWidget* content_box = GTK_WIDGET(user_data);
	GtkWidget* toggle_label =
		g_object_get_data(G_OBJECT(content_box), "toggle-label");
	gboolean visible = gtk_widget_get_visible(content_box);
	gtk_widget_set_visible(content_box, !visible);
	gtk_label_set_text(GTK_LABEL(toggle_label), visible ? "+" : "-");
}

static void BuildChannelTree(GtkWidget* parent_box, YampChannel* channels,
							 int nchannels, int depth, GtkWidget* root_box) {
	GtkWidget* current_listbox = NULL;
	if (!channels) {
		return;
	}
	for (int i = 0; i < nchannels; i++) {
		YampChannel* ch = &channels[i];
		gboolean is_category = ch->type == 1;

		if (is_category) {
			current_listbox = NULL;

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
			gtk_box_append(GTK_BOX(parent_box), header_row);

			GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
			gtk_box_append(GTK_BOX(parent_box), content_box);

			g_object_set_data(G_OBJECT(content_box), "toggle-label",
							  toggle_label);

			GtkGesture* toggleclick = gtk_gesture_click_new();
			g_signal_connect(toggleclick, "pressed",
							 G_CALLBACK(OnCategoryToggle), content_box);
			gtk_widget_add_controller(header_row,
									  GTK_EVENT_CONTROLLER(toggleclick));

			g_object_set_data_full(G_OBJECT(header_row), "category-name",
								   g_strdup(ch->id), g_free);

			GtkGesture* catclick = gtk_gesture_click_new();
			gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(catclick),
										  GDK_BUTTON_SECONDARY);
			g_signal_connect(catclick, "pressed",
							 G_CALLBACK(OnCategoryHeaderRightClick), NULL);
			gtk_widget_add_controller(header_row,
									  GTK_EVENT_CONTROLLER(catclick));

			BuildChannelTree(content_box, ch->children, ch->nchildren,
							 depth + 1, root_box);

			current_listbox = NULL;
		} else {
			if (current_listbox == NULL) {
				current_listbox = gtk_list_box_new();
				gtk_list_box_set_selection_mode(GTK_LIST_BOX(current_listbox),
												GTK_SELECTION_SINGLE);
				gtk_widget_set_margin_start(current_listbox, depth * 16);
				g_signal_connect(current_listbox, "row-selected",
								 G_CALLBACK(OnChannelSelected), NULL);
				gtk_box_append(GTK_BOX(parent_box), current_listbox);
			}

			YampChannel* chdup = malloc(sizeof(YampChannel));
			*chdup = *ch;
			chdup->name = strdup(chdup->name);
			char* dispname = ch->name;
			GtkWidget* lbr = gtk_list_box_row_new();
			GtkWidget* label = gtk_label_new(dispname);
			gtk_widget_set_halign(label, GTK_ALIGN_START);
			gtk_widget_add_css_class(lbr, "channel-row");
			GtkWidget* contentbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
			GtkWidget* hashtag =
				gtk_image_new_from_icon_name("text-channel-symbolic");
			gtk_box_append(GTK_BOX(contentbox), hashtag);
			gtk_box_append(GTK_BOX(contentbox), label);
			gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(lbr), contentbox);
			gtk_list_box_append(GTK_LIST_BOX(current_listbox), lbr);

			gtk_widget_set_margin_top(label, 5);
			gtk_widget_set_margin_bottom(label, 5);

			g_object_set_data(G_OBJECT(lbr), "channel", chdup);
		}
	}
}
int MainThreadChannelCB(gpointer data) {
	ChannelFetchPayload* cfp = data;
	GtkWidget* child;

	while ((child = gtk_widget_get_first_child(ChannelSidebar)) != NULL)
		gtk_widget_unparent(child);
	GtkWidget* headerbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_margin_top(headerbox, 10);
	gtk_widget_set_margin_bottom(headerbox, 10);
	GtkWidget* headerlbl = gtk_label_new(GetSpaceObject(curSpace)->displayname);
	gtk_widget_set_size_request(headerlbl, 180, -1);
	gtk_label_set_ellipsize(GTK_LABEL(headerlbl), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(headerlbl),
						 0.0); // keep text left-aligned as it shrinks
	gtk_widget_set_hexpand(headerlbl, FALSE);
	gtk_box_append(GTK_BOX(headerbox), headerlbl);

	GtkWidget* invbtn = gtk_button_new();
	GtkWidget* invimg = gtk_image_new_from_icon_name("invite-symbolic");
	gtk_button_set_child(GTK_BUTTON(invbtn), invimg);
	gtk_box_append(GTK_BOX(headerbox), invbtn);
	gtk_box_append(GTK_BOX(ChannelSidebar), headerbox);
	GtkWidget* channellist = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	BuildChannelTree(channellist, cfp->channels, cfp->nchannels, 0,
					 channellist);
	gtk_box_append(GTK_BOX(ChannelSidebar), channellist);

	free(data);
	return G_SOURCE_REMOVE;
}
void onYAMPChannelsFetched(YampChannel* channels, int nchannels) {
	ChannelFetchPayload* cfp = malloc(sizeof(ChannelFetchPayload));
	cfp->nchannels = nchannels;
	cfp->channels = channels;
	g_idle_add(MainThreadChannelCB, cfp);
}
void onYAMPChannelsUpdated(YampChannel* channels, int n, char* space) {
	if (curSpace && strcmp(space, curSpace) == 0) {
		ChannelFetchPayload* cfp = malloc(sizeof(ChannelFetchPayload));
		cfp->nchannels = n;
		cfp->channels = channels;
		g_idle_add(MainThreadChannelCB, cfp);
	}
}
typedef struct {
	char* id;
	status stat;
} StatusUpdatePayload;

static const char* StatusToColor(const char* statusStr) {
	if (strcmp(statusStr, "online") == 0)
		return "108020";
	if (strcmp(statusStr, "dnd") == 0)
		return "801020";
	if (strcmp(statusStr, "developing") == 0)
		return "00FFFF";
	if (strcmp(statusStr, "drawing") == 0)
		return "0080FF";
	if (strcmp(statusStr, "gaming") == 0)
		return "FF8000";
	return "808080";
}

static gboolean MainThreadStatusCB(gpointer data) {
	StatusUpdatePayload* payload = data;

	if (!listmode) {
		free(payload->id);
		free(payload);
		return G_SOURCE_REMOVE;
	}

	for (GtkWidget* row = gtk_widget_get_first_child(dmlist); row;
		 row = gtk_widget_get_next_sibling(row)) {
		GtkWidget* itemBox = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
		GtkWidget* pfp = gtk_widget_get_first_child(itemBox);
		if (!pfp)
			continue;
		GtkWidget* label = gtk_widget_get_next_sibling(pfp);
		if (!label)
			continue;

		const char* rowUsername = g_object_get_data(G_OBJECT(label), "id");
		if (rowUsername && strcmp(rowUsername, payload->id) == 0) {
			const char* color = StatusToColor(payload->stat.status);
			GtkCssProvider* provider = gtk_css_provider_new();
			char style[64];
			snprintf(style, sizeof(style),
					 "image { border: 2px solid #%s; -gtk-icon-size: 32px; }",
					 color);
			gtk_css_provider_load_from_string(provider, style);
			gtk_style_context_add_provider(
				gtk_widget_get_style_context(pfp), GTK_STYLE_PROVIDER(provider),
				GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
			break;
		}
	}

	free(payload->id);
	free(payload);
	return G_SOURCE_REMOVE;
}

void onYAMPStatusUpdate(char* id, status stat) {
	StatusUpdatePayload* payload = malloc(sizeof(StatusUpdatePayload));
	payload->id = strdup(id);
	payload->stat = stat;
	g_idle_add(MainThreadStatusCB, payload);
}
void UserDetailsBoxOnClick(gpointer none) { SpawnSettings(); }

void DMsListTopPagesCB(GtkListBox* lb, GtkListBoxRow* lbr, void* data) {
	gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "friends");
}

char* JoinUsernames(const YampUser* people, int n) {
	size_t len = 1;
	for (int i = 0; i < n; i++) {
		const char* nm =
			people[i].displayname ? people[i].displayname : people[i].username;
		len += strlen(nm) + (i ? 2 : 0);
	}

	char* out = malloc(len);
	if (!out)
		return NULL;

	char* p = out;
	for (int i = 0; i < n; i++) {
		const char* nm =
			people[i].displayname ? people[i].displayname : people[i].username;
		if (i) {
			*p++ = ',';
			*p++ = ' ';
		}
		size_t l = strlen(nm);
		memcpy(p, nm, l);
		p += l;
	}
	*p = '\0';
	return out;
}
int InsertConvIntoList(YampChannel conv) {
	if (conv.type == YAMP_DM) {
		YampUser* members = conv.people;
		YampUser usr; // the other user
		if (strcmp(members[0].id, curUserID) == 0) {
			usr = members[1];
		} else {
			usr = members[0];
		}
		char* dispname = usr.displayname ? usr.displayname : usr.username;
		YampUser* heapusr = malloc(sizeof(YampUser));
		*heapusr = (YampUser){0};
		if (usr.displayname) {
			heapusr->displayname = strdup(usr.displayname);
		}
		heapusr->username = strdup(usr.username);
		if (usr.pfp) {
			heapusr->pfp = strdup(usr.pfp);
		}
		strcpy(heapusr->id, usr.id);
		if (usr.description) {
			heapusr->description = strdup(usr.description);
		}
		InsertUserObject(heapusr->id, heapusr);
		GtkWidget* ItemBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		GtkWidget* LBRow = gtk_list_box_row_new();
		GtkWidget* LBRowLabel = gtk_label_new(usr.displayname);
		GtkWidget* Pfp;
		if (usr.pfp && strlen(usr.pfp)) {
			curl_easy_setopt(curl, CURLOPT_URL, usr.pfp);
			char* pfp_path = g_build_filename(pfp_dir, usr.id, NULL);
			FILE* fl = fopen(pfp_path, "wb");
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, fl);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
			curl_easy_perform(curl);
			fclose(fl);

			Pfp = gtk_image_new_from_file(pfp_path);
			gtk_widget_add_css_class(Pfp, "avatar");
			gtk_widget_set_overflow(Pfp, GTK_OVERFLOW_HIDDEN);

			InsertPfpPath(usr.id, pfp_path);

		} else {
			char pfppath[37];
			sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
					GetDefaultPfp(usr.id));
			Pfp = gtk_image_new_from_resource(pfppath);
		}
		char* statusClr = StatusToColor(usr.status.status);
		GtkCssProvider* provider = gtk_css_provider_new();
		char* style = malloc(60);
		sprintf(style, "image { border: 2px solid #%s; -gtk-icon-size: 32px; }",
				statusClr);
		gtk_css_provider_load_from_string(provider, style);
		gtk_style_context_add_provider(gtk_widget_get_style_context(Pfp),
									   GTK_STYLE_PROVIDER(provider),
									   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_set_data(G_OBJECT(LBRowLabel), "id", heapusr->id);
		g_object_set_data(G_OBJECT(LBRowLabel), "type", (void*)conv.type);
		gtk_box_append(GTK_BOX(ItemBox), Pfp);
		gtk_box_append(GTK_BOX(ItemBox), LBRowLabel);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(LBRow), ItemBox);
		gtk_widget_set_halign(LBRowLabel, GTK_ALIGN_START);
		gtk_list_box_append(GTK_LIST_BOX(dmlist), LBRow);
	} else if (conv.type == YAMP_GC) {
		// Currently the protocol doesnt fucking carry the name
		// when we do so the joined name will just be used foor null named ones
		YampUser* members = conv.people;
		char* dispname = JoinUsernames(members,conv.npeople);
		GtkWidget* ItemBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		GtkWidget* LBRow = gtk_list_box_row_new();
		GtkWidget* LBRowLabel = gtk_label_new(dispname);
		GtkWidget* Pfp;
		char pfppath[37];
		sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
				GetDefaultPfp(conv.id));
		Pfp = gtk_image_new_from_resource(pfppath);

		GtkCssProvider* provider = gtk_css_provider_new();
		char* style = malloc(60);
		sprintf(style, "image { -gtk-icon-size: 32px; }");
		gtk_css_provider_load_from_string(provider, style);
		gtk_style_context_add_provider(gtk_widget_get_style_context(Pfp),
									   GTK_STYLE_PROVIDER(provider),
									   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_set_data(G_OBJECT(LBRowLabel), "username", "Group Chat");
		g_object_set_data(G_OBJECT(LBRowLabel), "id", conv.id);
		g_object_set_data(G_OBJECT(LBRowLabel), "type", (void*)conv.type);
		gtk_box_append(GTK_BOX(ItemBox), Pfp);
		gtk_box_append(GTK_BOX(ItemBox), LBRowLabel);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(LBRow), ItemBox);
		gtk_widget_set_halign(LBRowLabel, GTK_ALIGN_START);
		gtk_list_box_append(GTK_LIST_BOX(dmlist), LBRow);
	}
	return 1;
}
void OnGCInitMembersSelected(YampUser* users, int nusers) {
	char** initmembers = malloc(sizeof(char*) * nusers);
	for (int i = 0; i < nusers; i++) {
		initmembers[i] = users[i].id;
	}
	YAMPCreateGC(mainsock, initmembers, nusers);
	free(initmembers);
}
void OnCreateGCBtnClicked(GtkButton* btn, gpointer user_data) {
	SpawnGCInitMemberSelector(cached_friends, ncached_friends,
							  OnGCInitMembersSelected);
}
void on_dms_btn_clicked(GtkButton* btn, gpointer user_data) {
	gtk_widget_add_css_class(dmsbutton, "active");
	gtk_widget_remove_css_class(selectedspacebtn, "active");
	selectedspacebtn = NULL;
	if (listmode == FALSE) {
		GtkWidget* child;
		while ((child = gtk_widget_get_first_child(ChannelSidebar)) != NULL)
			gtk_widget_unparent(child);
		dmlist = gtk_list_box_new();
		GtkWidget* toptabs = gtk_list_box_new();
		GtkWidget* friendscontent = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		gtk_box_append(GTK_BOX(friendscontent),
					   gtk_image_new_from_icon_name("dms"));
		gtk_box_append(GTK_BOX(friendscontent), gtk_label_new("Friends"));
		GtkWidget* friendstab = gtk_list_box_row_new();
		gtk_widget_add_css_class(friendstab, "toptabs");
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(friendstab),
								   friendscontent);
		g_signal_connect(toptabs, "row-selected", G_CALLBACK(DMsListTopPagesCB),
						 NULL);
		gtk_list_box_append(GTK_LIST_BOX(toptabs), friendstab);
		gtk_box_append(GTK_BOX(ChannelSidebar), toptabs);
		GtkWidget* dmheader_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		gtk_widget_add_css_class(dmheader_row, "channel-category");

		GtkWidget* dmheader = gtk_label_new("Direct Messages");
		gtk_widget_set_halign(dmheader, GTK_ALIGN_START);
		gtk_widget_set_hexpand(dmheader, TRUE);
		gtk_widget_add_css_class(dmheader, "channel-category");

		GtkWidget* newgc_btn =
			gtk_button_new_from_icon_name("list-add-symbolic");
		gtk_widget_add_css_class(newgc_btn, "flat");
		gtk_widget_add_css_class(newgc_btn, "circular");
		gtk_widget_set_valign(newgc_btn, GTK_ALIGN_CENTER);
		g_signal_connect(newgc_btn, "clicked", G_CALLBACK(OnCreateGCBtnClicked),
						 NULL);

		gtk_box_append(GTK_BOX(dmheader_row), dmheader);
		gtk_box_append(GTK_BOX(dmheader_row), newgc_btn);
		gtk_box_append(GTK_BOX(ChannelSidebar), dmheader_row);
		gtk_box_append(GTK_BOX(ChannelSidebar), dmlist);
		g_signal_connect(dmlist, "row-selected", G_CALLBACK(OnDMRowSelected),
						 NULL);
		for (int i = 0; i < ncached_convs; i++) {
			InsertConvIntoList(cached_convs[i]);
		}
		listmode = TRUE;
	}
}
char FetchedUserProfile = false;
GtkWidget* UsernameLabel;
void SendFQBtnCB(GtkButton* btn, void* data) {
	YAMPSendFriendReq(mainsock, gtk_entry_buffer_get_text(
									gtk_entry_get_buffer(GTK_ENTRY(data))));
	gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(data)), "", 0);
}
void SetupFriendsPane(GtkWidget* friendspane) {
	GtkWidget* mainvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget* fqsendarea = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkWidget* fqlabel = gtk_label_new(NULL);
	gtk_widget_set_margin_start(mainvbox, 8);
	gtk_widget_set_margin_end(mainvbox, 8);
	gtk_widget_set_margin_top(mainvbox, 8);
	gtk_widget_set_margin_bottom(mainvbox, 8);
	gtk_label_set_markup(
		GTK_LABEL(fqlabel),
		"<span size=\"large\" weight=\"bold\">Send a friend request</span>");
	gtk_box_append(GTK_BOX(mainvbox), fqlabel);
	GtkWidget* fqsendname = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(fqsendname),
								   "username or username@instance.xyz");
	gtk_widget_set_hexpand(fqsendarea, 1);
	gtk_widget_set_hexpand(fqsendname, 1);
	GtkWidget* fqsendbtn = gtk_button_new_with_label("Send");
	g_signal_connect(fqsendbtn, "clicked", G_CALLBACK(SendFQBtnCB), fqsendname);
	gtk_box_append(GTK_BOX(fqsendarea), fqsendname);
	gtk_box_append(GTK_BOX(fqsendarea), fqsendbtn);
	gtk_box_append(GTK_BOX(mainvbox), fqsendarea);
	gtk_box_append(GTK_BOX(friendspane), mainvbox);
	gtk_widget_set_hexpand(mainvbox, 1);
	gtk_widget_set_vexpand(mainvbox, 1);
}
typedef struct {
	char* username;
	GtkWidget* notif;
} fqnotifpayload;
void AcceptFQBtnCB(GtkButton* btn, void* data) {
	fqnotifpayload* payload = data;
	YAMPAcceptFriendReq(mainsock, payload->username);
	YAMPStartDM(mainsock, payload->username);
	gtk_widget_unparent(payload->notif);
	free(data);
}
void DenyFQBtnCB(GtkButton* btn, void* data) {
	fqnotifpayload* payload = data;
	YAMPDenyFriendReq(mainsock, payload->username);
	gtk_widget_unparent(payload->notif);
	free(data);
}
int CreateFQNotification(YampUser usr) {
	char* txt = malloc(25 + strlen(usr.displayname));
	fqnotifpayload* payload = malloc(sizeof(fqnotifpayload));
	payload->username = usr.username;
	sprintf(txt, "New friend request from %s", usr.displayname);
	GtkWidget* btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget* acceptbtn = gtk_button_new_with_label("Accept");
	g_signal_connect(acceptbtn, "clicked", G_CALLBACK(AcceptFQBtnCB), payload);
	gtk_box_append(GTK_BOX(btnbox), acceptbtn);
	GtkWidget* denybtn = gtk_button_new_with_label("Deny");
	g_signal_connect(denybtn, "clicked", G_CALLBACK(DenyFQBtnCB), payload);
	gtk_box_append(GTK_BOX(btnbox), denybtn);
	payload->notif = CreateClientNotification(notifoverlay, txt, btnbox);
	free(txt);
}
void onYAMPFriendRequestReceived(char* username) {
	YampUser usr;
	usr.displayname = username;
	usr.username = username;
	CreateFQNotification(usr);
}
void onYAMPFriendReqSent(int success) {}
void onYAMPFriendReqResolved(char* username, int success) {}

static gboolean scroll_tick(GtkWidget* w, GdkFrameClock* fc, gpointer data) {
	GtkAdjustment* adj =
		gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chatscroll));
	double target =
		gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
	gtk_adjustment_set_value(adj, target);

	static double last_upper = -1;
	double upper = gtk_adjustment_get_upper(adj);
	if (upper != last_upper) {
		last_upper = upper;
		return G_SOURCE_CONTINUE;
	}
	last_upper = -1;
	return G_SOURCE_REMOVE;
}

void ScrollChatToBottom(void) {
	gtk_widget_add_tick_callback(chatscroll, scroll_tick, NULL, NULL);
}

void UploadDialogFinish(GObject* source_object, GAsyncResult* result,
						gpointer user_data) {}
void OnUploadBtnClicked(GtkButton* btn, void* data) {
	GtkFileDialog* dlg = gtk_file_dialog_new();
	gtk_file_dialog_open(dlg, NULL, NULL, UploadDialogFinish, NULL);
}
void StartMainIMWindow(const YampLoginData* dat) {
	YampUser* heapusr = malloc(sizeof(YampUser));
	*heapusr = dat->usr;
	heapusr->username = strdup(heapusr->username);
	if (heapusr->displayname) {
		heapusr->displayname = strdup(heapusr->displayname);
	}
	if (heapusr->pfp) {
		heapusr->pfp = strdup(heapusr->pfp);
	}
	if (heapusr->description) {
		heapusr->description = strdup(heapusr->description);
	}
	printf("%s\n",dat->usr.id);
	strcpy(heapusr->id, dat->usr.id);
	InsertUserObject(heapusr->id, heapusr);
	char* displayName = dat->usr.displayname;
	if (!displayName) {
		displayName = dat->usr.username;
	}
	curUserID = heapusr->id;


	cached_convs = dat->conversations;
	ncached_convs = dat->nconversations;
	cached_friends = dat->friends;
	ncached_friends = dat->nfriends;
	const char* cache_dir = g_get_user_cache_dir();
	pfp_dir = g_build_filename(cache_dir, "yampen", "pfps", NULL);
	g_mkdir_with_parents(pfp_dir, 0700);
	notifoverlay = gtk_overlay_new();
	main_window = gtk_application_window_new(global_app);
	gtk_window_set_title(GTK_WINDOW(main_window), "Yampen");
	gtk_window_set_default_size(GTK_WINDOW(main_window), 960, 640);
	gtk_window_present(GTK_WINDOW(main_window));
	GtkWidget* panelhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_hexpand(panelhbox, TRUE);
	gtk_widget_set_vexpand(panelhbox, TRUE);
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_widget_set_hexpand(vbox, FALSE);
	GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);



	stackpane = gtk_stack_new();
	GtkWidget* chatpanehbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget* friendspane = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	SetupFriendsPane(friendspane);
	GtkWidget* chatvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	suvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_widget_set_hexpand(suvbox, 0);
	BuildUserProfile(dat->usr);
	gtk_widget_set_size_request(suvbox, 300, -1);
	// su stands for space & user here, had no better term, the box is for both
	// guild member listing and user profiles

	gtk_box_append(GTK_BOX(chatpanehbox), chatvbox);
	gtk_box_append(GTK_BOX(chatpanehbox), suvbox);
	gtk_stack_add_named(GTK_STACK(stackpane), chatpanehbox, "chat");
	gtk_stack_add_named(GTK_STACK(stackpane), friendspane, "friends");
	gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "chat");


	gtk_widget_set_hexpand(chatvbox, TRUE);

	GuildVBOX = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GuildList = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(GuildList),
									GTK_SELECTION_NONE);
	dmsbutton = gtk_button_new();
	gtk_widget_set_size_request(dmsbutton, 52, 52);
	gtk_widget_set_hexpand(dmsbutton, FALSE);
	gtk_widget_set_vexpand(dmsbutton, FALSE);
	gtk_widget_set_halign(dmsbutton, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(dmsbutton, GTK_ALIGN_START);

	GtkIconTheme* theme =
		gtk_icon_theme_get_for_display(gdk_display_get_default());

	gtk_icon_theme_add_resource_path(
		theme, "/org/yampen/assets/icons/hicolor/scalable/symbolic");

	GtkWidget* dmsbtnimg = gtk_image_new_from_icon_name("dms-symbolic");
	gtk_widget_add_css_class(dmsbutton, "active");
	gtk_image_set_pixel_size(GTK_IMAGE(dmsbtnimg), 28);
	gtk_button_set_child(GTK_BUTTON(dmsbutton), dmsbtnimg);
	g_signal_connect(dmsbutton, "clicked", G_CALLBACK(on_dms_btn_clicked),
					 NULL);
	GtkCssProvider* dmsprovider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(
		dmsprovider, "button {"
					 "	color: #f25858;"
					 "  background: rgba(255,255,255,0.15);"
					 "  border-radius: 16px;"
					 "  padding: 0;"
					 "  min-width: 48px;"
					 "  min-height: 48px;"
					 "  border: none;"
					 "  transition: color 200ms ease, border-radius 200ms "
					 "ease, background 200ms ease;"
					 "}"
					 "button:hover {"
					 "	color: #404040;"
					 "  background: #f25858;"
					 "  border-radius: 12px;"
					 "}"
					 "button.active {"
					 "	color: #404040;"
					 "  background: #f25858;"
					 "  border-radius: 12px;"
					 "}");
	gtk_style_context_add_provider(gtk_widget_get_style_context(dmsbutton),
								   GTK_STYLE_PROVIDER(dmsprovider),
								   GTK_STYLE_PROVIDER_PRIORITY_USER);

	gtk_box_append(GTK_BOX(GuildVBOX), dmsbutton);

	gtk_box_append(GTK_BOX(GuildVBOX), GuildList);
	gtk_widget_set_vexpand(GuildList, 1);

	GtkWidget* insertbtn = gtk_button_new();
	g_signal_connect(insertbtn, "clicked", G_CALLBACK(cb_insert_space), NULL);
	gtk_widget_set_halign(insertbtn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(insertbtn, GTK_ALIGN_END);
	gtk_widget_set_size_request(insertbtn, 52, 52);

	GtkWidget* insertbtnimg = gtk_image_new_from_icon_name("insert-symbolic");
	gtk_widget_add_css_class(insertbtnimg, "active");
	gtk_image_set_pixel_size(GTK_IMAGE(insertbtnimg), 28);
	gtk_button_set_child(GTK_BUTTON(insertbtn), insertbtnimg);
	gtk_style_context_add_provider(gtk_widget_get_style_context(insertbtn),
								   GTK_STYLE_PROVIDER(dmsprovider),
								   GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_box_append(GTK_BOX(GuildVBOX), insertbtn);

	gtk_box_append(GTK_BOX(hbox), GuildVBOX);

	gtk_overlay_set_child(GTK_OVERLAY(notifoverlay), panelhbox);
	gtk_box_append(GTK_BOX(panelhbox), vbox);
	gtk_box_append(GTK_BOX(vbox), hbox);

	ChannelSidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_vexpand(ChannelSidebar, TRUE);
	gtk_widget_set_size_request(ChannelSidebar, 180, -1);

	GtkGesture* sidebarclick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(sidebarclick),
								  GDK_BUTTON_SECONDARY);
	g_signal_connect(sidebarclick, "pressed",
					 G_CALLBACK(OnChannelSidebarRightClick), NULL);
	gtk_widget_add_controller(ChannelSidebar,
							  GTK_EVENT_CONTROLLER(sidebarclick));

	gtk_box_append(GTK_BOX(hbox), ChannelSidebar);
	gtk_box_append(GTK_BOX(panelhbox), stackpane);

	chattitle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
	gtk_box_append(GTK_BOX(chatvbox), chattitle);
	chatscroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chatscroll),
								   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_widget_set_hexpand(chatscroll, TRUE);
	gtk_widget_set_vexpand(chatscroll, TRUE);
	chatarea = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(chatarea), GTK_SELECTION_NONE);
	gtk_widget_set_hexpand(chatarea, TRUE);
	gtk_widget_set_vexpand(chatarea, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(chatscroll), chatarea);
	gtk_box_append(GTK_BOX(chatvbox), chatscroll);
	GtkWidget* inputhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(chatvbox), inputhbox);

	GtkWidget* frame_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_add_css_class(frame_box, "message-entry-frame");
	gtk_widget_set_margin_start(frame_box, 8);
	gtk_widget_set_margin_end(frame_box, 8);
	gtk_widget_set_margin_bottom(frame_box, 8);

	GtkWidget* plus_btn = gtk_button_new_from_icon_name("list-add-symbolic");
	gtk_widget_add_css_class(plus_btn, "flat");
	gtk_widget_add_css_class(plus_btn, "circular");
	gtk_widget_set_sensitive(plus_btn, YAMPQueryYAMPHTTP());
	g_signal_connect(plus_btn, "clicked", G_CALLBACK(OnUploadBtnClicked), NULL);

	GtkWidget* entry = gtk_text_view_new();
	gtk_widget_add_css_class(entry, "plain");
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(entry), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(entry), 8);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(entry), 8);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(entry), 8);
	gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(entry), 8);
	gtk_widget_set_hexpand(entry, TRUE);

	GtkWidget* scroller = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
								   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller),
											   200);
	gtk_scrolled_window_set_propagate_natural_height(
		GTK_SCROLLED_WINDOW(scroller), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), entry);

	GtkWidget* emoji_btn = gtk_button_new_from_icon_name("face-smile-symbolic");
	gtk_widget_add_css_class(emoji_btn, "flat");
	gtk_widget_add_css_class(emoji_btn, "circular");

	gtk_box_append(GTK_BOX(frame_box), plus_btn);
	gtk_box_append(GTK_BOX(frame_box), scroller);
	gtk_box_append(GTK_BOX(frame_box), emoji_btn);
	gtk_box_append(GTK_BOX(inputhbox), frame_box);

	EKeyPayload* entrydat = malloc(sizeof(EKeyPayload));
	entrydat->EntryArea = entry;
	entrydat->ChatView = chatarea;
	GtkEventController* key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(EntryKeyHandler),
					 entrydat);
	gtk_widget_add_controller(entry, GTK_EVENT_CONTROLLER(key_controller));

	if (dat->usr.pfp && strlen(dat->usr.pfp)) {
		curl_easy_setopt(curl, CURLOPT_URL, dat->usr.pfp);
		char* pfp_path = g_build_filename(pfp_dir, dat->usr.id, NULL);
		FILE* fl = fopen(pfp_path, "wb");
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fl);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
		curl_easy_perform(curl);
		fclose(fl);
		InsertPfpPath(dat->usr.id, pfp_path);
		SelfPfp = gtk_image_new_from_file(pfp_path);
	} else {
		char pfppath[37];
		sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
				GetDefaultPfp(dat->usr.id));
		SelfPfp = gtk_image_new_from_resource(pfppath);
	}
	gtk_widget_add_css_class(SelfPfp, "avatar");
	gtk_widget_set_overflow(SelfPfp, GTK_OVERFLOW_HIDDEN);
	GtkCssProvider* provider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(
		provider, "* { border: 2px solid #108020; -gtk-icon-size: 36px; }");
	// the user logs in as online by default so this is fixed
	// change when that statement changes

	GtkWidget* UserDetailsArea = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(UserDetailsArea, 0, 50);
	GtkWidget* UserDetailsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_hexpand(UserDetailsBox, 1);
	gtk_style_context_add_provider(gtk_widget_get_style_context(SelfPfp),
								   GTK_STYLE_PROVIDER(provider),
								   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	gtk_box_append(GTK_BOX(UserDetailsBox), SelfPfp);
	gtk_widget_set_valign(SelfPfp, GTK_ALIGN_CENTER);
	GtkWidget* UsernameBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(UsernameBox, GTK_ALIGN_CENTER);
	DisplayNameLabel = gtk_label_new(displayName);
	gtk_box_append(GTK_BOX(UserDetailsBox), UsernameBox);
	gtk_box_append(GTK_BOX(UsernameBox), DisplayNameLabel);
	if (dat->usr.displayname && strcmp(dat->usr.displayname, dat->usr.username) != 0) {
		UsernameLabel = gtk_label_new(dat->usr.username);
		gtk_widget_set_opacity(UsernameLabel, 0.6f);
		gtk_box_append(GTK_BOX(UsernameBox), UsernameLabel);
	}
	GtkWidget* usersettingsbtn = gtk_button_new();
	gtk_widget_add_css_class(usersettingsbtn, "flat");
	gtk_widget_add_css_class(usersettingsbtn, "circular");
	gtk_widget_set_valign(usersettingsbtn, GTK_ALIGN_CENTER);
	gtk_button_set_icon_name(GTK_BUTTON(usersettingsbtn), "emblem-system");
	g_signal_connect(usersettingsbtn, "clicked", G_CALLBACK(LaunchUserSettings),
					 NULL);

	gtk_box_append(GTK_BOX(UserDetailsArea), UserDetailsBox);
	gtk_box_append(GTK_BOX(UserDetailsArea), usersettingsbtn);
	gtk_box_append(GTK_BOX(vbox), UserDetailsArea);
	GtkGesture* click = gtk_gesture_click_new();
	g_signal_connect(click, "pressed", G_CALLBACK(UserDetailsBoxOnClick), NULL);
	gtk_widget_add_controller(UserDetailsBox, GTK_EVENT_CONTROLLER(click));
	LoadSpaces(dat->spaces, dat->nspaces);
	for (int i = 0; i < dat->fqcount; i++) {
		CreateFQNotification(dat->incfq[i]);
	}
	gtk_window_set_child(GTK_WINDOW(main_window), notifoverlay);
	on_dms_btn_clicked(GTK_BUTTON(dmsbutton), NULL);
}
typedef struct {
	YampUser* users;
	int nusers;
} MainThreadFriendPayload;
static gboolean MainThreadFriendCB(gpointer data) {
	/*
	MainThreadFriendPayload* payload = data;
	GtkWidget* child;
	while ((child = gtk_widget_get_first_child(ChannelSidebar)) != NULL)
		gtk_widget_unparent(child);
	dmlist = gtk_list_box_new();
	GtkWidget* toptabs = gtk_list_box_new();
	GtkWidget* friendscontent = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(friendscontent),
				   gtk_image_new_from_icon_name("dms"));
	gtk_box_append(GTK_BOX(friendscontent), gtk_label_new("Friends"));
	GtkWidget* friendstab = gtk_list_box_row_new();
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(friendstab), friendscontent);
	g_signal_connect(toptabs, "row-selected", G_CALLBACK(DMsListTopPagesCB),
					 NULL);
	gtk_list_box_append(GTK_LIST_BOX(toptabs), friendstab);
	gtk_box_append(GTK_BOX(ChannelSidebar), toptabs);
	gtk_box_append(GTK_BOX(ChannelSidebar), dmlist);
	g_signal_connect(dmlist, "row-selected", G_CALLBACK(OnDMRowSelected), NULL);
	for (int i = 0; i < payload->nusers; i++) {
		// InsertFriendIntoList(payload->users[i]);
	}
	free(payload->users);
	free(payload);
	return G_SOURCE_REMOVE;
	*/
}
gboolean MainThreadNewFriend(void* rawfriend) {
	YampUser* friend = rawfriend;
	// InsertFriendIntoList(*friend);
	free(friend);
	return G_SOURCE_REMOVE;
}
void onYAMPNewFriend(YampUser friend) {
	YampUser* heapfriend = malloc(sizeof(YampUser));
	*heapfriend = friend;
	g_idle_add(MainThreadNewFriend, heapfriend);
}
void onYAMPNewConversation(YampChannel conv) {
	YampChannel* heapconv = malloc(sizeof(YampChannel));
	*heapconv = conv;
	g_idle_add(MainThreadNewFriend, heapconv);
}
void onYAMPFriendsListed(YampUser* friends, int nfriends) {
	MainThreadFriendPayload* payload = malloc(sizeof(MainThreadFriendPayload));
	payload->users = friends;
	payload->nusers = nfriends;
	g_idle_add(MainThreadFriendCB, payload);
}

static gboolean MainThreadUserDetCB(gpointer data) {
	cJSON* Details = data;
	char* username = cJSON_GetObjectItem(Details, "name")->valuestring;
	char* display_name =
		cJSON_GetObjectItem(Details, "display_name")->valuestring;
	char* id = cJSON_GetObjectItem(Details, "id")->valuestring;
	YampUser* heapusr = malloc(sizeof(YampUser));
	heapusr->username = strdup(username);
	heapusr->displayname = strdup(display_name);
	InsertUserObject(heapusr->id, heapusr);
	curUsername = username;
	gtk_label_set_text(GTK_LABEL(DisplayNameLabel), display_name);
	if (UsernameLabel) {
		gtk_label_set_text(GTK_LABEL(UsernameLabel), username);
	}
	if (!cJSON_GetObjectItem(Details, "pfp")) {
		heapusr->pfp = NULL;
	} else {
		heapusr->pfp = cJSON_GetObjectItem(Details, "pfp")->valuestring;
		curl_easy_setopt(curl, CURLOPT_URL,
						 cJSON_GetObjectItem(Details, "pfp")->valuestring);
		char* pfp_path = g_build_filename(pfp_dir, username, NULL);
		FILE* fl = fopen(pfp_path, "wb");
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fl);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_cb);
		curl_easy_perform(curl);
		fclose(fl);
		InsertPfpPath(username, pfp_path);
	}
	return G_SOURCE_REMOVE;
}
void onYAMPUserDetailsFetched(cJSON* Details) {
	g_idle_add(MainThreadUserDetCB, Details);
	FetchedUserProfile = true;
}

typedef struct {
	char* id;
	char* data;
	char* where;
} IMReceivePayload;

static gboolean receive_im_main_thread(gpointer user_data) {
	IMReceivePayload* payload = user_data;

	if (currentChat && strcmp(currentChat, payload->where) == 0) {
		char* id;
		YampUser* usr = GetUserObject(payload->id);
		id = usr->id;
		if (!usr) {
			return G_SOURCE_REMOVE;
		}
		char* dispname = usr->displayname ? usr->displayname : usr->username;
		PushUIMessage(chatscroll, chatarea, id, dispname, NULL, payload->data);
		ScrollChatToBottom();
	} else {
		GtkWidget* notifcontent = gtk_label_new(payload->data);
		CreateClientNotification(notifoverlay, "New Message", notifcontent);
	}

	free(payload->id);
	free(payload->data);
	free(payload->where);
	free(payload);

	return G_SOURCE_REMOVE;
}
void onYAMPReceiveIM(char* author_id, char* where, char* data) {
	IMReceivePayload* payload = malloc(sizeof(IMReceivePayload));
	payload->id = strdup(author_id);
	payload->data = strdup(data);
	payload->where = strdup(where);
	g_idle_add(receive_im_main_thread, payload);
}