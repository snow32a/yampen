#include <stdio.h>
#include <gtk/gtk.h>
#include "imwnd.h"
#include "glib-object.h"
#include "login.h"
#include "chatwnd.h"
#include "settings.h"
#include "glibconfig.h"
#include "gtk/gtkshortcut.h"
#include "protocol/yamp.h"
#include "globals.h"
#include "hashtables.h"
#include <cjson/cJSON.h>
#include <curl/curl.h>
GtkWidget *main_window;
GtkWidget *BuddyList;
GtkWidget *dmsbutton;
GtkWidget *GuildVBOX;
GtkWidget *GuildList;
GtkWidget *SelfPfp;
GtkWidget *chatscroll;
GtkWidget *chatarea;
GtkWidget *DisplayNameLabel;
GtkWidget *UsernameLabel;
gboolean listmode = TRUE;
char *curSpace = NULL;
CURL *curl;
char *currentChat;
static gboolean onUIDisconnected(gpointer none) {
	GtkAlertDialog *alert = gtk_alert_dialog_new("Server disconnected!");
	DisplayLoginDialog(global_app);
	return G_SOURCE_REMOVE;
}

typedef struct {
	GtkWidget *EntryArea;
	GtkWidget *ChatView;
} EKeyPayload;
static gboolean EntryKeyHandler(GtkEventControllerKey *controller, guint keyval,
								guint keycode, GdkModifierType state,
								gpointer user_data) {

	printf("KEY: keyval=%u keycode=%u\n", keyval, keycode);

	if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) &&
		!(state & GDK_SHIFT_MASK)) {
		EKeyPayload *dat = (EKeyPayload *)user_data;
		GtkTextBuffer *buf =
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea));
		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter(buf, &start);
		gtk_text_buffer_get_end_iter(buf, &end);
		char *content = gtk_text_buffer_get_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea)), &start,
			&end, TRUE);
		YAMPSendIM(mainsock, currentChat, content);
		gtk_text_buffer_set_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea)), "", 0);
		return GDK_EVENT_STOP;
	}
	return FALSE;
}

void onYAMPDisconnected() { g_idle_add(onUIDisconnected, 0); }
void on_buddy_row_activated(GtkListBox *box, GtkListBoxRow *row,
							gpointer user_data) {
	if (currentChat) {
		free(currentChat);
	}
	if (listmode == TRUE) {

		GtkWidget *child = gtk_widget_get_next_sibling(
			gtk_widget_get_first_child(gtk_list_box_row_get_child(row)));
		char *name = gtk_label_get_text(GTK_LABEL(child));
		char *username = g_object_get_data(G_OBJECT(child), "username");
		currentChat = MakeDMChannel(username, curUsername);
		gtk_list_box_remove_all(GTK_LIST_BOX(chatarea));
		YAMPGetMessageHistory(mainsock, currentChat);
	} else {
		char *channelname =
			gtk_label_get_text(GTK_LABEL(gtk_list_box_row_get_child(row)));
		char *loc = malloc(1 + strlen(curSpace) + strlen(channelname) + 1);
		*loc = '^';
		strcpy(loc + 1, curSpace);
		strcpy(loc + strlen(curSpace) + 1, channelname);
		currentChat = loc;
		YAMPGetMessageHistory(mainsock, loc);
	}
}
GCallback on_space_row_activated(GtkListBox *box, GtkListBoxRow *row,
								 gpointer user_data) {
	gtk_widget_remove_css_class(dmsbutton, "active");
	listmode = FALSE;
	YAMPListSpaceChannels(mainsock, "yamp-central");
}
int MainThreadSpaceCB(gpointer data) {
	cJSON *Spaces = data;
	for (int i = 0; i < cJSON_GetArraySize(Spaces); i++) {
		cJSON *Space = cJSON_GetArrayItem(Spaces, i);
		printf("%s\n", cJSON_GetObjectItem(Space, "display_name")->valuestring);

		GtkWidget *gldbtn = gtk_list_box_row_new();
		GtkWidget *gldbtnimg =
			gtk_image_new_from_file("./assets/guildbase.png");

		gtk_image_set_pixel_size(GTK_IMAGE(gldbtnimg), 48);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(gldbtn), gldbtnimg);

		gtk_list_box_append(GTK_LIST_BOX(GuildList), gldbtn);
		g_object_set_data(G_OBJECT(gldbtn), "name",
						  cJSON_GetObjectItem(Space, "name")->valuestring);
		curSpace = strdup(cJSON_GetObjectItem(Space, "name")->valuestring);
	}
	return G_SOURCE_REMOVE;
}
void onYAMPSpacesFetched(cJSON *Spaces) {
	g_idle_add(MainThreadSpaceCB, Spaces);
}

int MainThreadChannelCB(gpointer data) {
	cJSON *Channels = data;
	gtk_list_box_remove_all(GTK_LIST_BOX(BuddyList));
	for (int i = 0; i < cJSON_GetArraySize(Channels); i++) {
		cJSON *Channel = cJSON_GetArrayItem(Channels, i);
		char *channelname = cJSON_GetObjectItem(Channel, "name")->valuestring;
		char *dispchannelname = malloc(strlen(channelname) + 2);
		*dispchannelname = '#';
		memcpy(dispchannelname + 1, channelname, strlen(channelname) + 1);
		GtkWidget *lbr = gtk_list_box_row_new();
		GtkWidget *label = gtk_label_new(dispchannelname);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(lbr), label);
		gtk_list_box_append(GTK_LIST_BOX(BuddyList), lbr);
	}

	return G_SOURCE_REMOVE;
}
typedef struct {
	char *username;
	status stat;
} StatusUpdatePayload;

static const char *StatusToColor(const char *statusStr) {
	if (strcmp(statusStr, "online") == 0) return "108020";
	if (strcmp(statusStr, "dnd") == 0) return "801020";
	if (strcmp(statusStr, "developing") == 0) return "00FFFF";
	if (strcmp(statusStr, "drawing") == 0) return "0080FF";
	if (strcmp(statusStr, "gaming") == 0) return "FF8000";
	return "808080";
}

static gboolean MainThreadStatusCB(gpointer data) {
	StatusUpdatePayload *payload = data;

	if (!listmode) {
		free(payload->username);
		free(payload);
		return G_SOURCE_REMOVE;
	}

	for (GtkWidget *row = gtk_widget_get_first_child(BuddyList); row;
		 row = gtk_widget_get_next_sibling(row)) {
		GtkWidget *itemBox = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
		GtkWidget *pfp = gtk_widget_get_first_child(itemBox);
		if (!pfp) continue;
		GtkWidget *label = gtk_widget_get_next_sibling(pfp);
		if (!label) continue;

		const char *rowUsername = g_object_get_data(G_OBJECT(label), "username");
		if (rowUsername && strcmp(rowUsername, payload->username) == 0) {
			const char *color = StatusToColor(payload->stat.status);
			GtkCssProvider *provider = gtk_css_provider_new();
			char style[64];
			snprintf(style, sizeof(style),
					 "image { border: 2px solid #%s; -gtk-icon-size: 32px; }",
					 color);
			gtk_css_provider_load_from_string(provider, style);
			gtk_style_context_add_provider(gtk_widget_get_style_context(pfp),
										   GTK_STYLE_PROVIDER(provider),
										   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
			break;
		}
	}

	free(payload->username);
	free(payload);
	return G_SOURCE_REMOVE;
}

void onYAMPStatusUpdate(char *name, status stat) {
	StatusUpdatePayload *payload = malloc(sizeof(StatusUpdatePayload));
	payload->username = strdup(name);
	payload->stat = stat;
	g_idle_add(MainThreadStatusCB, payload);
}
void onYAMPChannelsFetched(cJSON *Channels) {
	g_idle_add(MainThreadChannelCB, Channels);
}
GCallback UserDetailsBoxOnClick(gpointer none) {
	SpawnSettings();
	return G_SOURCE_REMOVE;
}
static void on_dms_btn_clicked(GtkButton *btn, gpointer user_data) {
	gtk_widget_add_css_class(dmsbutton, "active");
	gtk_list_box_unselect_row(GTK_LIST_BOX(GuildList),gtk_list_box_get_selected_row(GTK_LIST_BOX(GuildList)));

	if (listmode == FALSE) {
		YAMPListBuddies(mainsock);
		listmode = TRUE;
	}
}
char FetchedUserProfile = false;
GtkWidget *UsernameLabel;
void StartMainIMWindow() {
	main_window = gtk_application_window_new(global_app);
	gtk_window_set_title(GTK_WINDOW(main_window), "Yampen");
	gtk_window_set_default_size(GTK_WINDOW(main_window), 800, 600);
	GMenu *menu = g_menu_new();
	gtk_window_present(GTK_WINDOW(main_window));
	GtkWidget *panelhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_hexpand(panelhbox, TRUE);
	gtk_widget_set_vexpand(panelhbox, TRUE);
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	GtkWidget *chatvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_widget_set_hexpand(chatvbox, TRUE);

	GuildVBOX = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GuildList = gtk_list_box_new();

	dmsbutton = gtk_button_new();
	gtk_widget_set_size_request(dmsbutton, 52, 52);
	gtk_widget_set_hexpand(dmsbutton, FALSE);
	gtk_widget_set_vexpand(dmsbutton, FALSE);
	GtkIconTheme *theme =
		gtk_icon_theme_get_for_display(gdk_display_get_default());

	const char *paths[] = {"assets/icons", NULL};

	gtk_icon_theme_set_search_path(theme, paths);

	GtkWidget *dmsbtnimg = gtk_image_new_from_icon_name("dms-symbolic");
	gtk_widget_add_css_class(dmsbutton, "active");
	gtk_image_set_pixel_size(GTK_IMAGE(dmsbtnimg), 28);
	gtk_button_set_child(GTK_BUTTON(dmsbutton), dmsbtnimg);
	g_signal_connect(dmsbutton, "clicked", G_CALLBACK(on_dms_btn_clicked),
					 NULL);

	GtkCssProvider *dmsprovider = gtk_css_provider_new();
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

	GtkWidget *insertbtn = gtk_button_new();
	gtk_widget_set_size_request(insertbtn, 52, 52);

	GtkWidget *insertbtnimg = gtk_image_new_from_icon_name("insert-symbolic");
	gtk_widget_add_css_class(insertbtnimg, "active");
	gtk_image_set_pixel_size(GTK_IMAGE(insertbtnimg), 28);
	gtk_button_set_child(GTK_BUTTON(insertbtn), insertbtnimg);
	gtk_style_context_add_provider(gtk_widget_get_style_context(insertbtn),
								   GTK_STYLE_PROVIDER(dmsprovider),
								   GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_box_append(GTK_BOX(GuildVBOX), insertbtn);

	gtk_box_append(GTK_BOX(hbox), GuildVBOX);

	gtk_window_set_child(GTK_WINDOW(main_window), panelhbox);
	gtk_box_append(GTK_BOX(panelhbox), vbox);
	gtk_box_append(GTK_BOX(panelhbox), chatvbox);
	gtk_box_append(GTK_BOX(vbox), hbox);

	g_signal_connect(GuildList, "row-activated",
					 G_CALLBACK(on_space_row_activated), NULL);

	BuddyList = gtk_list_box_new();
	gtk_widget_set_vexpand(BuddyList, TRUE);
	gtk_widget_set_size_request(BuddyList, 180, -1);

	gtk_box_append(GTK_BOX(hbox), BuddyList);
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(BuddyList),
									GTK_SELECTION_SINGLE); // i need hlep
	g_signal_connect(BuddyList, "row-activated",
					 G_CALLBACK(on_buddy_row_activated), NULL);

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
	GtkWidget *inputhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(chatvbox), inputhbox);

	GtkWidget *upload_btn = gtk_button_new_with_label("+");
	gtk_box_append(GTK_BOX(inputhbox), upload_btn);
	GtkWidget *entry = gtk_text_view_new();
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_box_append(GTK_BOX(inputhbox), entry);

	EKeyPayload *dat = malloc(sizeof(EKeyPayload));
	dat->EntryArea = entry;
	dat->ChatView = chatarea;
	GtkEventController *key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(EntryKeyHandler),
					 dat);
	gtk_widget_add_controller(entry, GTK_EVENT_CONTROLLER(key_controller));



	GtkWidget *UserDetailsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	char *displayName = GetDisplayName(curUsername);
	if (!displayName) {
		displayName = curUsername;
	}

	SelfPfp = gtk_image_new_from_file(GetPfpPath(curUsername));
	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(
		provider, "* { border: 2px solid #108020; -gtk-icon-size: 36px; }");

	gtk_style_context_add_provider(gtk_widget_get_style_context(SelfPfp),
								   GTK_STYLE_PROVIDER(provider),
								   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	gtk_box_append(GTK_BOX(UserDetailsBox), SelfPfp);
	GtkWidget* UsernameBox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
	UsernameLabel = gtk_label_new(displayName);
	DisplayNameLabel = gtk_label_new(curUsername);
	gtk_box_append(GTK_BOX(UserDetailsBox), UsernameBox);
	gtk_box_append(GTK_BOX(UsernameBox), DisplayNameLabel);
	gtk_box_append(GTK_BOX(UsernameBox), UsernameLabel);
	gtk_box_append(GTK_BOX(vbox), UserDetailsBox);
	GtkGesture *click = gtk_gesture_click_new();
	g_signal_connect(click, "pressed", G_CALLBACK(UserDetailsBoxOnClick), NULL);
	gtk_widget_add_controller(UserDetailsBox, GTK_EVENT_CONTROLLER(click));
}
static gboolean MainThreadBuddyCB(gpointer data) {
	cJSON *Buddies = data;
	gtk_list_box_remove_all(GTK_LIST_BOX(BuddyList));
	for (int i = 0; i < cJSON_GetArraySize(Buddies); i++) {
		cJSON *Buddy = cJSON_GetArrayItem(Buddies, i);
		const char *Name = cJSON_GetObjectItem(Buddy, "name")->valuestring;
		const char *DisplayName =
			cJSON_GetObjectItem(Buddy, "display_name")->valuestring;
		InsertDisplayName(Name, DisplayName);
		if (cJSON_GetObjectItem(Buddy, "description")) {
			InsertProfileDescription(
				Name, cJSON_GetObjectItem(Buddy, "description")->valuestring);
		}
		GtkWidget *ItemBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		GtkWidget *LBRow = gtk_list_box_row_new();
		GtkWidget *LBRowLabel = gtk_label_new(DisplayName);
		GtkWidget *Pfp;
		if (!cJSON_GetObjectItem(Buddy, "pfp")) {
			Pfp = gtk_image_new_from_file("./pfp.png");
			InsertPfpPath(Name, "./pfp.png");
		} else {
			curl_easy_setopt(curl, CURLOPT_URL,
							 cJSON_GetObjectItem(Buddy, "pfp")->valuestring);
			size_t len = 10 + strlen(Name);
			char *filePath = malloc(len);
			sprintf(filePath, "/tmp/%s.png", Name);
			FILE *fl = fopen(filePath, "wb");
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, fl);
			curl_easy_perform(curl);
			fclose(fl);
			printf("%s\n", filePath);
			Pfp = gtk_image_new_from_file(filePath);
			InsertPfpPath(Name, filePath);
		}
		char *statusClr;
		if (strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(Buddy, "status"),
									   "status")
					   ->valuestring,
				   "online") == 0) {
			statusClr = "108020";
		} else if (strcmp(cJSON_GetObjectItem(
							  cJSON_GetObjectItem(Buddy, "status"), "status")
							  ->valuestring,
						  "dnd") == 0) {
			statusClr = "00C010";
		} else if (strcmp(cJSON_GetObjectItem(
							  cJSON_GetObjectItem(Buddy, "status"), "status")
							  ->valuestring,
						  "developing") == 0) {
			statusClr = "00FFFF";
		}

		else if (strcmp(cJSON_GetObjectItem(
							cJSON_GetObjectItem(Buddy, "status"), "status")
							->valuestring,
						"drawing") == 0) {
			statusClr = "00C0FF";
		}

		else if (strcmp(cJSON_GetObjectItem(
							cJSON_GetObjectItem(Buddy, "status"), "status")
							->valuestring,
						"gaming") == 0) {
			statusClr = "FF0080";
		} else {
			statusClr = "808080";
		}
		GtkCssProvider *provider = gtk_css_provider_new();
		char *style = malloc(59);
		sprintf(style, "image { border: 2px solid #%s; -gtk-icon-size: 32px; }",
				statusClr);
		gtk_css_provider_load_from_string(provider, style);
		gtk_style_context_add_provider(gtk_widget_get_style_context(Pfp),
									   GTK_STYLE_PROVIDER(provider),
									   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_set_data(G_OBJECT(LBRowLabel), "username", (gpointer)Name);
		gtk_box_append(GTK_BOX(ItemBox), Pfp);
		gtk_box_append(GTK_BOX(ItemBox), LBRowLabel);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(LBRow), ItemBox);
		gtk_widget_set_halign(LBRowLabel, GTK_ALIGN_START);
		gtk_list_box_append(GTK_LIST_BOX(BuddyList), LBRow);
	}

	return G_SOURCE_REMOVE;
}
void onYAMPBuddyListed(cJSON *Buddies) {
	g_idle_add(MainThreadBuddyCB, Buddies);
}

static gboolean MainThreadUserDetCB(gpointer data) {
	cJSON *Details = data;
	char *username = cJSON_GetObjectItem(Details, "name")->valuestring;
	char *display_name =
		cJSON_GetObjectItem(Details, "display_name")->valuestring;
	InsertDisplayName(username, display_name);
	curUsername = username;
	gtk_label_set_text(GTK_LABEL(DisplayNameLabel), display_name);
	gtk_label_set_text(GTK_LABEL(UsernameLabel), username);
	if (!cJSON_GetObjectItem(Details, "pfp")) {
		InsertPfpPath(username, "./pfp.png");
	} else {
		curl_easy_setopt(curl, CURLOPT_URL,
						 cJSON_GetObjectItem(Details, "pfp")->valuestring);
		size_t len = 10 + strlen(username);
		char *filePath = malloc(len);
		sprintf(filePath, "/tmp/%s.png", username);
		FILE *fl = fopen(filePath, "wb");
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fl);
		curl_easy_perform(curl);
		fclose(fl);
		printf("%s\n", filePath);
		InsertPfpPath(username, filePath);
	}
	return G_SOURCE_REMOVE;
}
void onYAMPUserDetailsFetched(cJSON *Details) {
	g_idle_add(MainThreadUserDetCB, Details);
	FetchedUserProfile = true;
}

typedef struct {
	char *username;
	char *data;
	char *where;
} IMReceivePayload;

static gboolean receive_im_main_thread(gpointer user_data) {
	IMReceivePayload *payload = user_data;
	if (strcmp(currentChat, payload->where) == 0) {
		char *username;
		username = GetDisplayName(payload->username);
		if (!username) {
			username = payload->username;
		}
		PushUIMessage(chatscroll, chatarea, payload->username, username,
					  payload->data);
	}
	return G_SOURCE_REMOVE;
}

void onYAMPReceiveIM(char *username, char *where, char *data) {
	IMReceivePayload *payload = malloc(sizeof(IMReceivePayload));
	payload->username = strdup(username);
	payload->data = strdup(data);
	payload->where = strdup(where);
	g_idle_add(receive_im_main_thread, payload);
}
