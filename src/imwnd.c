#include <stdio.h>
#include <gtk/gtk.h>
#include "imwnd.h"
#include "gdk/gdkkeysyms.h"
#include "gio/gio.h"
#include "gio/gmenu.h"
#include "glib-object.h"
#include "glib.h"
#include <login.h>
#include <yamp.h>
#include "pango/pango-attributes.h"
#include <settings.h>
#include "glibconfig.h"
#include "gtk/gtkshortcut.h"
#include <yamp.h>
#include <globals.h>
#include <hashtables.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include "insertspace.h"
#include "notification.h"
#include <usersettings.h>
#include <gcmemselect.h>
#include "emoji_map.h"
#include "chatinput.h"
#include "types.h"
#include "invitedialog.h"
#include "gcsettings.h"
#include <chatwnd.h>
#include <avatar.h>
#include <config.h>
#include <imwnd_parts.h>
#include <channeltree.h>
#include <memberslist.h>
#include <userprofile.h>

char* dup0(const char* s) { return s ? strdup(s) : NULL; }

void onYAMPChannelsFetched(YampChannel** channels, int nchannels);
void onYAMPMessageHistoryLoaded(YampMessage** msgs, int nmsgs, int hasmore);

GQueue MessageStack = G_QUEUE_INIT;
YampChannel* currentChannel = NULL;
int isReplying = 0;
YampMessage* ReplyingToMsg = NULL;

static GPtrArray* typing_users = NULL;
void TypingClear(void);

gboolean listmode = FALSE;
char* curSpace = NULL;
char* curSpaceDisplay = NULL;
CURL* curl;
char* pfp_dir;
int MembersListShown = 0;

AttachementEntry* curAttachements = NULL;
GtkSizeGroup* topbar_group;

static gboolean onUIDisconnected(gpointer none) {
	GtkAlertDialog* alert =
		gtk_alert_dialog_new("The current server has went down!");
	gtk_alert_dialog_show(alert, NULL);
	gtk_window_destroy(GTK_WINDOW(main_window));
	DisplayLoginDialog(global_app);
	return G_SOURCE_REMOVE;
}

YampChannel** cached_convs = NULL;
int ncached_convs = 0;
YampUser** cached_friends = NULL;
int ncached_friends = 0;

void onYAMPDisconnected() { g_idle_add(onUIDisconnected, 0); }

void SetStatusFrameColor(GtkWidget* frame, const char* hex) {
	GtkCssProvider* p = g_object_get_data(G_OBJECT(frame), "status-provider");
	if (!p) {
		p = gtk_css_provider_new();
		gtk_style_context_add_provider(gtk_widget_get_style_context(frame),
									   GTK_STYLE_PROVIDER(p),
									   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_set_data_full(G_OBJECT(frame), "status-provider", p,
							   g_object_unref);
	}
	char* css = g_strdup_printf(".pfp-frame { border: 2px solid #%s; "
								"border-radius: 50%%; padding: 2px; }",
								hex);
	gtk_css_provider_load_from_string(p, css);
	g_free(css);
}

GtkWidget* WrapInStatusFrame(GtkWidget* img, int px, const char* hex) {
	gtk_image_set_pixel_size(GTK_IMAGE(img), px);
	gtk_widget_set_size_request(img, px, px);
	gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(img, GTK_ALIGN_CENTER);
	gtk_widget_set_overflow(img, GTK_OVERFLOW_HIDDEN);

	GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(frame, "pfp-frame");
	gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(frame, GTK_ALIGN_CENTER);
	gtk_widget_set_overflow(frame, GTK_OVERFLOW_HIDDEN);
	gtk_box_append(GTK_BOX(frame), img);

	SetStatusFrameColor(frame, hex);
	return frame;
}

void OnGCTitleGesture(GtkGestureClick* gesture, int n_press, double x, double y,
					  gpointer user_data) {
	SpawnGCEdit(GTK_WINDOW(main_window), (char*)user_data);
}
void OnGCAddMembersCB(YampUser** users, int n) {
	if (currentChannel->type != YAMP_GC) {
		return;
	}
	for (int i = 0; i < n; i++) {
		YAMPAddMemberToGC(mainsock, users[i]->id, currentChannel->id, NULL);
	}
}
void OnGCInviteBtn(GtkButton* btn, void* data) {
	SpawnGCInitMemberSelector(cached_friends, ncached_friends,
							  OnGCAddMembersCB);
}
void OnUIKickGCMember(YampUser* usr, void* dat) {
	YAMPGCKick(mainsock, usr->id, dat, NULL);
}
void OnUITransferGCOwnership(YampUser* usr, void* dat) {
	YAMPGCTransferOwnership(mainsock, usr->id, dat, NULL);
}
void OnDMRowSelected(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
	gtk_list_box_unselect_all(GTK_LIST_BOX(toptabs));
	gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "chat");
	if (!row) {
		return;
	}
	if (currentChannel) {
		YAMPLeaveChannel(mainsock, currentChannel->id, NULL);
	}
	TypingClear();
	GtkWidget* wdg;
	while ((wdg = gtk_widget_get_first_child(suvbox)) != NULL)
		gtk_widget_unparent(wdg);
	GtkWidget* child = GTK_WIDGET(row);

	currentChannel = DupChannelObject(
		(YampChannel*)g_object_get_data(G_OBJECT(row), "channel"));
	int type = (int)g_object_get_data(G_OBJECT(child), "type");
	LastTypedAt = 0;
	if (type == YAMP_DM) {
		memberlist = NULL;
		BuildUserProfile(
			suvbox, GetUserObject(g_object_get_data(G_OBJECT(child), "id")));
		MembersListShown = 0;
		GtkWidget* titlewdg;
		while ((titlewdg = gtk_widget_get_first_child(chattitle)) != NULL)
			gtk_widget_unparent(titlewdg);

		GtkWidget* titlebox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_margin_start(titlebox, 10);
		gtk_widget_set_valign(chattitle, GTK_ALIGN_CENTER);
		char* dpyname = GetUserObject(g_object_get_data(G_OBJECT(child), "id"))
							->displayname;
		char* username =
			GetUserObject(g_object_get_data(G_OBJECT(child), "id"))->username;
		GtkWidget* recipientname = gtk_label_new(dpyname ? dpyname : username);
		gtk_box_append(GTK_BOX(titlebox), recipientname);
		gtk_box_append(GTK_BOX(chattitle), titlebox);
		YAMPGetMessageHistory(mainsock, currentChannel->id, 50, NULL,
							  onYAMPMessageHistoryLoaded);
		YAMPJoinChannel(mainsock, currentChannel->id, NULL);
	} else if (type == YAMP_GC) {
		GtkWidget* titlewdg;
		while ((titlewdg = gtk_widget_get_first_child(chattitle)) != NULL)
			gtk_widget_unparent(titlewdg);

		MembersListShown = 1;

		GtkWidget* titlebox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_margin_start(titlebox, 10);
		gtk_widget_set_valign(chattitle, GTK_ALIGN_CENTER);
		GtkWidget* name_wdg =
			RecursiveFindWidgetByData(GTK_WIDGET(row), "tag", "name");



		GtkGesture* title_gesture = gtk_gesture_click_new();
		g_signal_connect_data(
			title_gesture, "released", G_CALLBACK(OnGCTitleGesture),
			g_strdup(currentChannel->id), (GClosureNotify)g_free, 0);


		GtkWidget* recipientname =
			gtk_label_new(gtk_label_get_text(GTK_LABEL(name_wdg)));
		gtk_label_set_xalign(GTK_LABEL(recipientname), 0.0f);
		gtk_widget_set_hexpand(GTK_WIDGET(recipientname), TRUE);

		gtk_widget_add_controller(recipientname,
								  GTK_EVENT_CONTROLLER(title_gesture));

		GtkWidget* invbtn = gtk_button_new_from_icon_name("invite-symbolic");
		g_signal_connect(invbtn, "clicked", G_CALLBACK(OnGCInviteBtn), NULL);
		gtk_widget_set_halign(invbtn, GTK_ALIGN_CENTER);
		gtk_widget_set_valign(invbtn, GTK_ALIGN_CENTER);

		gtk_box_append(GTK_BOX(titlebox), recipientname);
		gtk_box_append(GTK_BOX(chattitle), titlebox);
		gtk_box_append(GTK_BOX(chattitle), invbtn);
		MemberSet* members = g_object_get_data(G_OBJECT(child), "members");
		if (members) {
			BuildGCMemberList(members, currentChannel->owner);
		}
		SetMemberListActionCallbacks(OnUIKickGCMember, OnUITransferGCOwnership,
									 currentChannel->id);
		YAMPGetMessageHistory(mainsock, currentChannel->id, 50, NULL,
							  onYAMPMessageHistoryLoaded);
		YAMPJoinChannel(mainsock, currentChannel->id, NULL);
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
	LastTypedAt = 0;
	gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "chat");
	if (!row) {
		return;
	}
	if (currentChannel) {
		YAMPLeaveChannel(mainsock, currentChannel->id, NULL);
	}
	TypingClear();
	RecursiveDeselectChannelTree(ChannelSidebar, GTK_WIDGET(box));
	currentChannel = DupChannelObject(
		(YampChannel*)g_object_get_data(G_OBJECT(row), "channel"));

	GtkWidget* titlewdg;
	while ((titlewdg = gtk_widget_get_first_child(chattitle)) != NULL)
		gtk_widget_unparent(titlewdg);

	GtkWidget* titlebox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_margin_start(titlebox, 10);
	gtk_widget_set_valign(chattitle, GTK_ALIGN_CENTER);
	GtkWidget* channel_icon =
		gtk_image_new_from_icon_name("text-channel-symbolic");
	gtk_image_set_pixel_size(GTK_IMAGE(channel_icon), 16);
	GtkWidget* channel_title = gtk_label_new(currentChannel->name);
	gtk_box_append(GTK_BOX(titlebox), channel_icon);
	gtk_box_append(GTK_BOX(titlebox), channel_title);
	gtk_box_append(GTK_BOX(chattitle), titlebox);

	YAMPJoinChannel(mainsock, currentChannel->id, NULL);
	YAMPGetMessageHistory(mainsock, currentChannel->id, 50, NULL,
						  onYAMPMessageHistoryLoaded);
}
void SpaceRowCB(GtkButton* btn, gpointer user_data) {
	gtk_widget_remove_css_class(dmsbutton, "active");
	if (selectedspacebtn)
		gtk_widget_remove_css_class(selectedspacebtn, "active");
	gtk_widget_add_css_class(GTK_WIDGET(btn), "active");
	selectedspacebtn = GTK_WIDGET(btn);
	listmode = FALSE;
	YampSpace* space = user_data;
	curSpace = space->id;
	YampChannel** cached_channels;
	int ncached_channels = GetSpaceChannelCache(space->id, &cached_channels);
	if (cached_channels) {
		LoadSpaceChannels(cached_channels, ncached_channels);
	} else {
		YAMPListSpaceChannels(mainsock, curSpace, onYAMPChannelsFetched);
	}
}
int InsertSpace(YampSpace space) {
	GtkWidget* gldbtnrow = gtk_list_box_row_new();
	GtkWidget* spacebtn = gtk_button_new();
	YampSpace* heapspace = malloc(sizeof(YampSpace));
	*heapspace = space;
	strcpy(heapspace->id, space.id);
	g_signal_connect(spacebtn, "clicked", G_CALLBACK(SpaceRowCB), heapspace);
	InsertSpaceObject(strdup(space.id), heapspace);
	gtk_widget_set_size_request(spacebtn, 4, 48);
	gtk_widget_set_hexpand(spacebtn, FALSE);
	gtk_widget_set_vexpand(spacebtn, FALSE);
	gtk_widget_set_halign(spacebtn, GTK_ALIGN_CENTER);
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
	gtk_widget_set_hexpand(lbl, FALSE);
	gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
	gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_NONE);

	int avail = 48 - 10;
	PangoLayout* layout = gtk_widget_create_pango_layout(lbl, initials);
	int textw, texth;
	pango_layout_get_pixel_size(layout, &textw, &texth);
	g_object_unref(layout);

	if (textw > avail) {
		double scale = (double)avail / (double)textw;
		if (scale < 0.4)
			scale = 0.4; // don't go unreadably small
		PangoAttrList* attrs = pango_attr_list_new();
		pango_attr_list_insert(attrs, pango_attr_scale_new(scale));
		gtk_label_set_attributes(GTK_LABEL(lbl), attrs);
		pango_attr_list_unref(attrs);
	}

	free(initials);
	gtk_button_set_child(GTK_BUTTON(spacebtn), lbl);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(gldbtnrow), spacebtn);

	gtk_widget_add_css_class(spacebtn, "space_button_noaccent");

	gtk_list_box_append(GTK_LIST_BOX(GuildList), gldbtnrow);
	g_object_set_data(G_OBJECT(gldbtnrow), "name", space.name);
	g_object_set_data(G_OBJECT(gldbtnrow), "id", space.id);
	return 1;
}
gboolean MainThreadNewSpace(void* heapspace) {
	InsertSpace(*(YampSpace*)heapspace);
	free(heapspace);
	return G_SOURCE_REMOVE;
}
void onYAMPNewSpace(YampSpace space) {
	YampSpace* heapspace = malloc(sizeof(space));
	*heapspace = DupSpaceObject(&space);
	g_idle_add(MainThreadNewSpace, heapspace);
}

typedef struct {
	YampChannel** channels;
	int nchannels;
} ChannelFetchPayload;
static void OnInviteBtnClicked(GtkButton* btn, gpointer data) {
	if (!curSpace)
		return;
	YampSpace* sp = GetSpaceObject(curSpace);
	SpawnInviteDialog(GTK_WINDOW(main_window), curSpace,
					  sp ? sp->displayname : NULL);
}
int LoadSpaceChannels(YampChannel** channels, int nchannels) {
	GtkWidget* child;

	channeltree_root = NULL;
	while ((child = gtk_widget_get_first_child(ChannelSidebar)) != NULL)
		gtk_widget_unparent(child);
	GtkWidget* headerbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(headerbox, -1, TOPBAR_HEIGHT);
	gtk_widget_set_valign(headerbox, GTK_ALIGN_START);
	GtkWidget* headerlbl = gtk_label_new(GetSpaceObject(curSpace)->displayname);
	gtk_widget_set_size_request(headerlbl, 240, -1);
	gtk_label_set_ellipsize(GTK_LABEL(headerlbl), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(headerlbl),
						 0.0); // keep text left-aligned as it shrinks
	gtk_widget_set_hexpand(headerlbl, FALSE);
	gtk_box_append(GTK_BOX(headerbox), headerlbl);

	GtkWidget* invbtn = gtk_button_new_from_icon_name("invite-symbolic");
	g_signal_connect(invbtn, "clicked", G_CALLBACK(OnInviteBtnClicked), NULL);
	gtk_widget_set_halign(invbtn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(invbtn, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(headerbox), invbtn);
	gtk_size_group_add_widget(topbar_group, headerbox);
	gtk_box_append(GTK_BOX(ChannelSidebar), headerbox);
	GtkWidget* channellist_scr = gtk_scrolled_window_new();
	gtk_widget_set_hexpand(channellist_scr, TRUE);
	gtk_widget_set_vexpand(channellist_scr, TRUE);

	GtkWidget* channellist = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	g_object_set_data(G_OBJECT(channellist), "depth", GINT_TO_POINTER(0));
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(channellist_scr),
								  channellist);

	GtkGesture* sidebarclick = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(sidebarclick),
								  GDK_BUTTON_SECONDARY);
	g_signal_connect(sidebarclick, "pressed",
					 G_CALLBACK(OnChannelSidebarRightClick), NULL);
	gtk_widget_add_controller(ChannelSidebar,
							  GTK_EVENT_CONTROLLER(sidebarclick));

	BuildChannelTree(channellist, channels, nchannels, 0);
	channeltree_root = channellist;
	g_free(channeltree_space);
	channeltree_space = g_strdup(curSpace);
	gtk_box_append(GTK_BOX(ChannelSidebar), channellist_scr);
	return G_SOURCE_REMOVE;
}
int MainThreadChannelCB(ChannelFetchPayload* dat) {
	LoadSpaceChannels(dat->channels, dat->nchannels);
	UpdateSpaceChannelCache(dat->channels, dat->nchannels, curSpace);
	free(dat);
	return G_SOURCE_REMOVE;
}
void onYAMPChannelsFetched(YampChannel** channels, int nchannels) {
	ChannelFetchPayload* cfp = malloc(sizeof(ChannelFetchPayload));

	cfp->nchannels = nchannels;
	cfp->channels = DupChannelArray(channels, nchannels);
	g_idle_add(G_SOURCE_FUNC(MainThreadChannelCB), cfp);
}
typedef struct {
	char* id;
	YampStatus* stat;
} StatusUpdatePayload;

const char* StatusToColor(const char* statusStr) {
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

void UpdateFrameInList(GtkWidget* listbox, const char* id, const char* color,
					   gboolean frame_first) {
	for (GtkWidget* row = gtk_widget_get_first_child(listbox); row;
		 row = gtk_widget_get_next_sibling(row)) {
		GtkWidget* box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
		GtkWidget* frame = gtk_widget_get_first_child(box);
		if (!frame)
			continue;
		const char* rid = g_object_get_data(G_OBJECT(frame), "user-id");
		if (rid && strcmp(rid, id) == 0) {
			SetStatusFrameColor(frame, color);
			return;
		}
	}
}

gboolean MainThreadStatusCB(gpointer data) {
	StatusUpdatePayload* p = data;
	printf("%s stat | %s userid", p->stat->status, p->id);
	const char* st = p->stat->status ? p->stat->status : "offline";
	const char* color = StatusToColor(st);

	YampUser* u = GetUserObject(p->id);
	if (u) {
		if (u->status)
			FreeStatusObject(u->status);
		u->status = DupStatusObject(p->stat);
	}

	if (listmode && dmlist)
		UpdateFrameInList(dmlist, p->id, color, TRUE);
	if (MembersListShown && memberlist)
		UpdateFrameInList(memberlist, p->id, color, TRUE);
	if (curUserID && strcmp(p->id, curUserID) == 0)
		SetStatusFrameColor(SelfPfpFrame, color);

	free(p->id);
	FreeStatusObject(p->stat);
	free(p);
	return G_SOURCE_REMOVE;
}

void onYAMPStatusUpdate(char* id, YampStatus* stat) {
	StatusUpdatePayload* payload = malloc(sizeof(StatusUpdatePayload));
	payload->id = strdup(id);
	payload->stat = DupStatusObject(stat);
	g_idle_add(MainThreadStatusCB, payload);
}
void UserDetailsBoxOnClick(gpointer none) { SpawnSettings(); }

void DMsListTopPagesCB(GtkListBox* lb, GtkListBoxRow* lbr, void* data) {
	gtk_list_box_unselect_all(GTK_LIST_BOX(dmlist));
	gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "friends");
}

void OnGCInitMembersSelected(YampUser** users, int nusers) {
	char** initmembers = malloc(sizeof(char*) * nusers);
	for (int i = 0; i < nusers; i++) {
		initmembers[i] = users[i]->id;
	}
	YAMPCreateGC(mainsock, initmembers, nusers, NULL);
	free(initmembers);
}
void OnCreateGCBtnClicked(GtkButton* btn, gpointer user_data) {
	SpawnGCInitMemberSelector(cached_friends, ncached_friends,
							  OnGCInitMembersSelected);
}
void on_dms_btn_clicked(GtkButton* btn, gpointer user_data) {
	gtk_widget_add_css_class(dmsbutton, "active");
	if (selectedspacebtn)
		gtk_widget_remove_css_class(selectedspacebtn, "active");
	selectedspacebtn = NULL;
	if (listmode == FALSE) {
		GListModel* ctrls = gtk_widget_observe_controllers(ChannelSidebar);
		for (guint i = 0; i < g_list_model_get_n_items(ctrls); i++) {
			GtkEventController* c = g_list_model_get_item(ctrls, i);
			if (GTK_IS_GESTURE_CLICK(c)) {
				gtk_widget_remove_controller(ChannelSidebar, c);
				g_object_unref(c);
				break;
			}
			g_object_unref(c);
		}
		g_object_unref(ctrls);
		GtkWidget* child;
		while ((child = gtk_widget_get_first_child(ChannelSidebar)) != NULL)
			gtk_widget_unparent(child);
		GtkWidget* scrolledwnd = gtk_scrolled_window_new();
		gtk_widget_set_hexpand(scrolledwnd, TRUE);
		gtk_widget_set_vexpand(scrolledwnd, TRUE);
		dmlist = gtk_list_box_new();
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolledwnd), dmlist);
		toptabs = gtk_list_box_new();
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
		gtk_box_append(GTK_BOX(ChannelSidebar), scrolledwnd);
		g_signal_connect(dmlist, "row-selected", G_CALLBACK(OnDMRowSelected),
						 NULL);
		for (int i = 0; i < ncached_convs; i++) {
			InsertConvIntoList(cached_convs[i]);
		}
		listmode = TRUE;
	}
}
char FetchedUserProfile = false;
void SendFQBtnCB(GtkButton* btn, void* data) {
	YAMPSendFriendReq(
		mainsock,
		gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(data))), NULL);
	gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(data)), "", 0);
}
static char* pending_dm_user = NULL;
void OnFriendDMButtonClicked(GtkButton* btn, YampUser* user) {
	if (!user || !dmlist)
		return;

	GtkWidget* row = FindConvRow(user->id, YAMP_DM);
	if (row) {
		if (gtk_list_box_get_selected_row(GTK_LIST_BOX(dmlist)) ==
			GTK_LIST_BOX_ROW(row)) {
			gtk_list_box_unselect_all(GTK_LIST_BOX(toptabs));
			gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "chat");
		} else {
			gtk_list_box_select_row(GTK_LIST_BOX(dmlist),
									GTK_LIST_BOX_ROW(row));
		}
		return;
	}

	// no DM exists yet, so create one and open it when the channel arrives
	free(pending_dm_user);
	pending_dm_user = strdup(user->id);
	YAMPStartDM(mainsock, user->id, NULL);
}
void SetupFriendsPane(GtkWidget* friendspane) {
	GtkWidget* mainvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	GtkWidget* fqsendarea = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkWidget* fqlabel = gtk_label_new(NULL);
	gtk_widget_set_margin_start(mainvbox, 16);
	gtk_widget_set_margin_end(mainvbox, 16);
	gtk_widget_set_margin_top(mainvbox, 16);
	gtk_widget_set_margin_bottom(mainvbox, 16);
	gtk_label_set_markup(
		GTK_LABEL(fqlabel),
		"<span size=\"large\" weight=\"bold\">Send a friend request</span>");
	gtk_box_append(GTK_BOX(mainvbox), fqlabel);
	GtkWidget* fqsendname = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(fqsendname),
								   "username or username@instance.xyz");
	gtk_widget_set_hexpand(fqsendarea, 1);
	gtk_widget_set_hexpand(fqsendname, 1);
	GtkWidget* fqsendbtn = gtk_button_new_with_label("Send!");
	g_signal_connect(fqsendbtn, "clicked", G_CALLBACK(SendFQBtnCB), fqsendname);
	gtk_box_append(GTK_BOX(fqsendarea), fqsendname);
	gtk_box_append(GTK_BOX(fqsendarea), fqsendbtn);
	gtk_box_append(GTK_BOX(mainvbox), fqsendarea);

	GtkWidget* list_scr = gtk_scrolled_window_new();
	friendslist = gtk_list_box_new();
	for (int i = 0; i < ncached_friends; i++) {
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
		GtkWidget* name_lbl = gtk_label_new(cached_friends[i]->displayname
												? cached_friends[i]->displayname
												: cached_friends[i]->username);
		gtk_widget_set_hexpand(name_lbl, 1);
		GtkWidget* dm_btn = gtk_button_new_from_icon_name("mail-message-new");
		g_signal_connect(dm_btn, "clicked", G_CALLBACK(OnFriendDMButtonClicked),
						 cached_friends[i]);
		gtk_box_append(GTK_BOX(box), name_lbl);
		gtk_box_append(GTK_BOX(box), dm_btn);
		gtk_list_box_append(GTK_LIST_BOX(friendslist),
							box); // automatically wraps it as a row
	}
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scr), friendslist);
	gtk_box_append(GTK_BOX(mainvbox), list_scr);
	gtk_widget_set_vexpand(list_scr, 1);

	gtk_box_append(GTK_BOX(friendspane), mainvbox);
	gtk_widget_set_hexpand(mainvbox, 1);
	gtk_widget_set_vexpand(mainvbox, 1);
}
typedef struct {
	char* username;
	char* id;
	GtkWidget* notif;
} fqnotifpayload;

static void FQPayloadFree(fqnotifpayload* p) {
	free(p->id);
	free(p->username);
	free(p);
}

void AcceptFQBtnCB(GtkButton* btn, void* data) {
	fqnotifpayload* payload = data;
	YAMPAcceptFriendReq(mainsock, payload->id, NULL);
	YAMPStartDM(mainsock, payload->id, NULL);
	gtk_widget_unparent(payload->notif);
	FQPayloadFree(payload);
}
void DenyFQBtnCB(GtkButton* btn, void* data) {
	fqnotifpayload* payload = data;
	YAMPDenyFriendReq(mainsock, payload->id, NULL);
	gtk_widget_unparent(payload->notif);
	FQPayloadFree(payload);
}

int CreateFQNotification(YampUser* usr) {
	char* txt = malloc(25 + strlen(usr->displayname));
	fqnotifpayload* payload = malloc(sizeof(fqnotifpayload));
	payload->username = strdup(usr->username);
	payload->id = strdup(usr->id);
	sprintf(txt, "New friend request from %s", usr->displayname);
	GtkWidget* btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget* acceptbtn = gtk_button_new_with_label("Accept");
	g_signal_connect(acceptbtn, "clicked", G_CALLBACK(AcceptFQBtnCB), payload);
	gtk_box_append(GTK_BOX(btnbox), acceptbtn);
	GtkWidget* denybtn = gtk_button_new_with_label("Deny");
	g_signal_connect(denybtn, "clicked", G_CALLBACK(DenyFQBtnCB), payload);
	gtk_box_append(GTK_BOX(btnbox), denybtn);
	payload->notif = CreateClientNotification(notifoverlay, txt, btnbox);
	free(txt);
	FreeUserObject(usr);
}
void onYAMPFriendRequestReceived(YampUser* usr) {
	CreateFQNotification(DupUserObject(usr));
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
void onYAMPFileUploaded(char* filename, int success) {
	// printf("Uploaded file: %s, Success: %d\n",filename,success);
}
typedef struct {
	AttachementEntry* entry;
	GtkWidget* overlay;
} DeleteAttachPayload;
void ShowAttachMenu(GtkWidget* menu) { gtk_widget_set_visible(menu, TRUE); }
void HideAttachMenu(GtkWidget* menu) { gtk_widget_set_visible(menu, FALSE); }

void OnDeleteAttachement(GtkButton* btn, DeleteAttachPayload* data) {
	GtkWidget* overlay = GTK_WIDGET(data->overlay);
	GtkWidget* parent = gtk_widget_get_parent(overlay);
	if (parent)
		gtk_box_remove(GTK_BOX(parent), overlay);

	if (data->entry->before) {
		data->entry->before->after = data->entry->after;
	}
	if (data->entry->after) {
		data->entry->after->before = data->entry->before;
	}
}
void UploadDialogFinish(GObject* source_object, GAsyncResult* result,
						gpointer user_data) {
	GtkFileDialog* dialog = GTK_FILE_DIALOG(source_object);

	GError* error = NULL;
	GFile* file = gtk_file_dialog_open_finish(dialog, result, &error);

	if (!file) {
		if (error)
			g_error_free(error);
		return;
	}

	char* path = g_file_get_path(file);
	char* buf = NULL;
	gsize len = 0;

	if (path && g_file_load_contents(file, NULL, &buf, &len, NULL, &error)) {
		gboolean uncertain;
		char* basename = g_file_get_basename(file);
		char* mime =
			g_content_type_guess(basename, (guchar*)buf, len, &uncertain);
		char* mime_type = g_content_type_get_mime_type(mime);
		g_free(mime_type);
		g_free(mime);
		g_free(basename);
		g_free(buf);

		GdkPixbuf* pb =
			gdk_pixbuf_new_from_file_at_scale(path, -1, 120, TRUE, NULL);
		GtkWidget* attachementoverlay = gtk_overlay_new();
		GtkWidget* attachement = pb ? gtk_picture_new_for_pixbuf(pb)
									: gtk_picture_new_for_file(file);
		GtkWidget* hovermenu = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
		gtk_widget_set_vexpand(hovermenu, 0);
		gtk_widget_set_valign(hovermenu, GTK_ALIGN_START);
		gtk_widget_set_halign(hovermenu, GTK_ALIGN_END);
		GtkWidget* deletebtn =
			gtk_button_new_from_icon_name("user-trash-symbolic");
		gtk_box_append(GTK_BOX(hovermenu), deletebtn);
		if (pb)
			g_object_unref(pb);

		gtk_picture_set_content_fit(GTK_PICTURE(attachement),
									GTK_CONTENT_FIT_CONTAIN);
		gtk_widget_set_halign(attachement, GTK_ALIGN_START);
		gtk_widget_set_valign(attachement, GTK_ALIGN_START);
		gtk_widget_add_css_class(attachement, "attachment");
		gtk_overlay_set_child(GTK_OVERLAY(attachementoverlay), attachement);
		gtk_overlay_add_overlay(GTK_OVERLAY(attachementoverlay), hovermenu);
		gtk_box_append(GTK_BOX(AttachementsBox), attachementoverlay);

		DeleteAttachPayload* deletepayload = malloc(sizeof(*deletepayload));
		GtkEventController* motion = gtk_event_controller_motion_new();
		g_signal_connect_swapped(motion, "enter", G_CALLBACK(ShowAttachMenu),
								 hovermenu);
		g_signal_connect_swapped(motion, "leave", G_CALLBACK(HideAttachMenu),
								 hovermenu);
		gtk_widget_add_controller(attachementoverlay, motion);

		g_signal_connect(deletebtn, "clicked", G_CALLBACK(OnDeleteAttachement),
						 deletepayload);

		AttachementEntry* attachentry = g_new0(AttachementEntry, 1);
		attachentry->path = strdup(path);
		free(path);
		path = NULL;
		deletepayload->overlay = attachementoverlay;
		deletepayload->entry = attachentry;

		AttachementEntry* lastentry = curAttachements;
		if (lastentry) {
			while (lastentry->after)
				lastentry = lastentry->after;
			lastentry->after = attachentry;
		} else {
			curAttachements = attachentry;
		}
		attachentry->before = lastentry;

		gtk_widget_set_visible(AttachementsBox, TRUE);
	} else if (error) {
		g_error_free(error);
	}

	g_free(path);
	g_object_unref(file);
}
void OnUploadBtnClicked(GtkButton* btn, void* data) {
	GtkFileDialog* dlg = gtk_file_dialog_new();
	gtk_file_dialog_open(dlg, NULL, NULL, UploadDialogFinish, NULL);
}

void TypingEntryFree(gpointer data) {
	TypingEntry* e = data;
	if (e->timeout)
		g_source_remove(e->timeout);
	free(e->id);
	free(e->name);
	free(e);
}

void TypingRefreshLabel(void) {
	if (!typingindicator)
		return;
	int n = typing_users ? typing_users->len : 0;
	if (n == 0) {
		gtk_label_set_text(GTK_LABEL(typingindicator), "");
		gtk_widget_set_visible(typingindicator, 0);
		return;
	}
	gtk_widget_set_visible(typingindicator, 1);
	char* s;
	if (n == 1) {
		TypingEntry* a = g_ptr_array_index(typing_users, 0);
		s = malloc(14 + strlen(a->name));
		sprintf(s, "%s is typing...", a->name);
	} else if (n < 8) {
		int string_len = 15 + n - 1;
		for (int i = 0; i < n; i++) {
			TypingEntry* a = g_ptr_array_index(typing_users, i);
			string_len += strlen(a->name);
		}
		s = malloc(string_len + 1);
		char* curstr = s;
		for (int i = 0; i < n; i++) {
			TypingEntry* a = g_ptr_array_index(typing_users, i);
			strcpy(curstr, a->name);
			if (i != n - 1) {
				curstr[strlen(a->name)] = ' ';
				curstr++;
			}
			curstr += strlen(a->name);
		}
		strcpy(curstr, " are typing...");
	} else {
		s = strdup("Several people are typing...");
	}
	gtk_label_set_text(GTK_LABEL(typingindicator), s);
	free(s);
}

void TypingClear(void) {
	if (typing_users)
		g_ptr_array_set_size(typing_users, 0); // frees entries + their timers
	TypingRefreshLabel();
}

void TypingRemove(const char* id) {
	if (!typing_users)
		return;
	for (guint i = 0; i < typing_users->len; i++) {
		TypingEntry* e = g_ptr_array_index(typing_users, i);
		if (strcmp(e->id, id) == 0) {
			g_ptr_array_remove_index(typing_users, i);
			TypingRefreshLabel();
			return;
		}
	}
}

gboolean TypingExpire(gpointer data) {
	TypingEntry* e = data;
	e->timeout = 0;
	TypingRemove(e->id);
	return G_SOURCE_REMOVE;
}

void TypingAdd(const char* id, const char* name) {
	if (!typing_users)
		typing_users = g_ptr_array_new_with_free_func(TypingEntryFree);

	for (guint i = 0; i < typing_users->len; i++) {
		TypingEntry* e = g_ptr_array_index(typing_users, i);
		if (strcmp(e->id, id) == 0) {
			g_source_remove(e->timeout);
			e->timeout =
				g_timeout_add_seconds(TYPING_TIMEOUT_SECONDS, TypingExpire, e);
			return;
		}
	}
	TypingEntry* e = calloc(1, sizeof(TypingEntry));
	e->id = strdup(id);
	e->name = strdup(name);
	e->timeout = g_timeout_add_seconds(TYPING_TIMEOUT_SECONDS, TypingExpire, e);
	g_ptr_array_add(typing_users, e);
	TypingRefreshLabel();
}
typedef struct {
	YampUser* users;
	int nusers;
} MainThreadFriendPayload;
gboolean MainThreadNewFriend(void* rawfriend) {
	YampUser* friend = rawfriend;
	// InsertFriendIntoList(*friend);
	free(friend);
	return G_SOURCE_REMOVE;
}
void onYAMPNewFriend(YampUser* friend) {
	YampUser* heapfriend = DupUserObject(friend);
	g_idle_add(MainThreadNewFriend, heapfriend);
}
GtkWidget* RecursiveFindWidgetByData(GtkWidget* parent, const char* key,
									 const char* val) {
	for (GtkWidget* w = gtk_widget_get_first_child(parent); w;
		 w = gtk_widget_get_next_sibling(w)) {
		const char* v = g_object_get_data(G_OBJECT(w), key);
		if (v && strcmp(v, val) == 0)
			return w;
		GtkWidget* r = RecursiveFindWidgetByData(w, key, val);
		if (r)
			return r;
	}
	return NULL;
}

static void InsertSpaceChannel(YampChannel* ch) {
	if (!channeltree_root || !channeltree_space || !ch->space ||
		strcmp(ch->space, channeltree_space) != 0)
		return;
	if (RecursiveFindWidgetByData(channeltree_root, "channel-id", ch->id))
		return;

	GtkWidget* container = channeltree_root;
	int depth = 0;
	if (ch->parent && *ch->parent) {
		container = RecursiveFindWidgetByData(channeltree_root, "category-id",
											  ch->parent);
		if (!container)
			return;
		depth =
			GPOINTER_TO_INT(g_object_get_data(G_OBJECT(container), "depth"));
	}

	GtkWidget* entry = BuildChannelEntry(ch, depth);

	int pos = ch->pos < 0 ? G_MAXINT : ch->pos;
	GtkWidget* prev = NULL;
	GtkWidget* w = gtk_widget_get_first_child(container);
	for (int i = 0; w && i < pos; i++) {
		prev = w;
		w = gtk_widget_get_next_sibling(w);
	}
	gtk_box_insert_child_after(GTK_BOX(container), entry, prev);
}
gboolean MainThreadNewChannel(YampChannel* heapconv) {
	if (heapconv->type == YAMP_DM || heapconv->type == YAMP_GC) {
		InsertConvIntoList(heapconv);
		if (heapconv->type == YAMP_DM && pending_dm_user) {
			for (int i = 0; i < heapconv->npeople; i++) {
				if (strcmp(heapconv->people[i]->id, pending_dm_user) == 0) {
					GtkWidget* row = FindConvRow(pending_dm_user, YAMP_DM);
					if (row)
						gtk_list_box_select_row(GTK_LIST_BOX(dmlist),
												GTK_LIST_BOX_ROW(row));
					free(pending_dm_user);
					pending_dm_user = NULL;
					break;
				}
			}
		}
		cached_convs =
			realloc(cached_convs, sizeof(YampChannel*) * (ncached_convs + 1));
		cached_convs[ncached_convs] = DupChannelObject(heapconv);
		ncached_convs++;
	} else if (YAMP_IS_SPACE_TYPE(heapconv->type)) {
		InsertSpaceChannel(heapconv);
	}

	FreeChannelObject(heapconv);
	return G_SOURCE_REMOVE;
}
void onYAMPNewChannel(YampChannel* conv) {
	YampChannel* heapconv = DupChannelObject(conv);
	g_idle_add(G_SOURCE_FUNC(MainThreadNewChannel), heapconv);
}
gboolean MainThreadDeleteChannel(char* id) {
	// since we only get an ID, we try to just check everywhere until we destroy
	// it space channels arnet cached yet, you'd need to invalidate cache once
	// its found
	if (DeleteChannelEntry(channeltree_root, id)) {
		return G_SOURCE_REMOVE;
	}
	for (int i = 0; i < ncached_convs; i++) {
		if (strcmp(cached_convs[i]->id, id) == 0) {
			// be gone, jerk!
			DeleteConvFromList(id);
			FreeChannelObject(cached_convs[i]);

			ncached_convs--;

			if (i != ncached_convs) {
				cached_convs[i] = cached_convs[ncached_convs];
			}
			break;
		}
	}
	free(id);
	return G_SOURCE_REMOVE;
}
void onYAMPChannelDeleted(char* id) {
	g_idle_add(G_SOURCE_FUNC(MainThreadDeleteChannel), dup0(id));
}


gboolean MainThreadChannelUpdated(YampChannel* heapconv) {
	if (heapconv->type == YAMP_DM || heapconv->type == YAMP_GC) {
		if (heapconv->type == YAMP_GC &&
			strcmp(heapconv->id, currentChannel->id) == 0) {
			SetMemberOwner(heapconv->owner);
		}
		for (int i = 0; i < ncached_convs; i++) {
			if (strcmp(cached_convs[i]->id, heapconv->id) == 0) {
				FreeChannelObject(cached_convs[i]);
				cached_convs[i] = DupChannelObject(heapconv);
				break;
			}
		}
		ModifyConvInList(heapconv);
	} else if (YAMP_IS_SPACE_TYPE(heapconv->type)) {
	}
	FreeChannelObject(heapconv);
	return G_SOURCE_REMOVE;
}
void onYAMPChannelUpdated(YampChannel* conv) {
	YampChannel* heapconv = DupChannelObject(conv);
	g_idle_add(G_SOURCE_FUNC(MainThreadChannelUpdated), heapconv);
}

void onYAMPFriendsListed(YampUser* friends, int nfriends) {}
typedef struct {
	char* id;
	char* name;
	char* channel;
} TypingPayload;

static gboolean MainThreadTypingCB(gpointer data) {
	TypingPayload* p = data;
	if (currentChannel && strcmp(currentChannel->id, p->channel) == 0) {
		TypingAdd(p->id, p->name);
	}
	free(p->id);
	free(p->name);
	free(p->channel);
	free(p);
	return G_SOURCE_REMOVE;
}

void onYAMPTyping(YampUser* usr, char* channel) {
	TypingPayload* p = malloc(sizeof(TypingPayload));
	p->id = strdup(usr->id);
	p->name = strdup(usr->displayname ? usr->displayname : usr->username);
	p->channel = strdup(channel);
	g_idle_add(MainThreadTypingCB, p);
}

typedef struct {
	YampMessage* msg;
	int nonce;
} IMReceivePayload;

static gboolean receive_im_main_thread(gpointer user_data) {
	IMReceivePayload* payload = user_data;

	if (currentChannel &&
		strcmp(currentChannel->id, payload->msg->channel) == 0) {
		YampUser* usr = payload->msg->author;
		if (!usr) {
			return G_SOURCE_REMOVE;
		}

		TypingRemove(usr->id);

		if (payload->nonce != -1) {
			GtkWidget* silhouette = GetMessageSilhouettes(payload->nonce);
			if (silhouette) {
				GtkWidget* row =
					gtk_widget_get_ancestor(silhouette, GTK_TYPE_LIST_BOX_ROW);
				if (row && gtk_widget_get_parent(row) == chatarea)
					gtk_list_box_remove(GTK_LIST_BOX(chatarea), row);
			}
		}
		PushUIMessage(chatscroll, chatarea, payload->msg, 0);
		ScrollChatToBottom();
	} else {
		GtkWidget* notifcontent = gtk_label_new(payload->msg->content);
		CreateClientNotification(notifoverlay, "New Message", notifcontent);
	}
	FreeMessageObject(payload->msg);
	free(payload);

	return G_SOURCE_REMOVE;
}
void onYAMPReceiveIM(YampMessage* msg, int nonce) {
	IMReceivePayload* payload = malloc(sizeof(IMReceivePayload));
	payload->msg = DupMessageObject(msg);
	payload->nonce = nonce;
	g_idle_add(receive_im_main_thread, payload);
}
typedef struct {
	YampMessage** msgs;
	int nmsgs;
	int hasmore;
} HistoryLoadPayload;
gboolean HistoryLoadedMainThread(HistoryLoadPayload* payload) {
	gtk_list_box_remove_all(GTK_LIST_BOX(chatarea));
	if (!payload->hasmore) {
		PushChannelBanner(chatarea, currentChannel);
	}
	for (int i = 0; i < payload->nmsgs; i++) {
		YampUser* usr = payload->msgs[i]->author;
		if (!usr) {
			return G_SOURCE_REMOVE;
		}
		PushUIMessage(chatscroll, chatarea, payload->msgs[i], 0);
	}
	ScrollChatToBottom();
	for (int i = 0; i < payload->nmsgs; i++)
		FreeMessageObject(payload->msgs[i]);
	free(payload->msgs);
	free(payload);

	return G_SOURCE_REMOVE;
}
void onYAMPMessageHistoryLoaded(YampMessage** msgs, int nmsgs, int hasmore) {
	HistoryLoadPayload* payload = calloc(1, sizeof(HistoryLoadPayload));
	if (!payload)
		return;

	payload->msgs = calloc(nmsgs > 0 ? nmsgs : 1, sizeof(YampMessage*));
	if (!payload->msgs) {
		free(payload);
		return;
	}

	for (int i = 0; i < nmsgs; i++) {
		YampMessage* copy = DupMessageObject(msgs[i]);
		if (!copy)
			continue;
		payload->msgs[payload->nmsgs++] = copy;
	}
	payload->hasmore = hasmore;

	g_idle_add(G_SOURCE_FUNC(HistoryLoadedMainThread), payload);
}
gboolean MessageDeleted_MainThread(
	char* message_id) { // well i just casted the func below rather than passing
						// a GPointer in the arguments
	DeleteUIMessage(chatscroll, chatarea, message_id);
	ScrollChatToBottom();
	free(message_id);
	return G_SOURCE_REMOVE;
}
void onYAMPMessageDeleted(char* message_id) {
	g_idle_add((GSourceFunc)MessageDeleted_MainThread, strdup(message_id));
}
typedef struct {
	YampUser* user;
	char* channel_id;
} MainThreadCJLPayload;
gboolean ChannelJoined_MainThread(MainThreadCJLPayload* payload) {
	if (payload->channel_id && payload->user && currentChannel &&
		strcmp(currentChannel->id, payload->channel_id) == 0) {
		const char* name = payload->user->displayname
							   ? payload->user->displayname
							   : payload->user->username;
		GtkWidget* row = gtk_list_box_row_new();
		char* safe_name = g_markup_escape_text(name ? name : "?", -1);
		char* markup = g_strdup_printf("<span foreground=\"#2ecc71\">➜</span> "
									   "<b>%s</b> joined this channel",
									   safe_name);
		GtkWidget* label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(label), markup);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_wrap(GTK_LABEL(label), TRUE);
		gtk_widget_set_margin_start(label, 6);
		gtk_widget_set_margin_end(label, 6);
		gtk_widget_set_margin_top(label, 2);
		gtk_widget_set_margin_bottom(label, 2);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
		gtk_list_box_append(GTK_LIST_BOX(chatarea), row);
		g_free(markup);
		g_free(safe_name);
	}
	free(payload->channel_id);
	if (payload->user)
		FreeUserObject(payload->user);
	free(payload);
	return G_SOURCE_REMOVE;
}
gboolean ChannelLeft_MainThread(MainThreadCJLPayload* payload) {
	if (payload->channel_id && payload->user && currentChannel &&
		strcmp(currentChannel->id, payload->channel_id) == 0) {
		const char* name = payload->user->displayname
							   ? payload->user->displayname
							   : payload->user->username;
		GtkWidget* row = gtk_list_box_row_new();
		char* safe_name = g_markup_escape_text(name ? name : "?", -1);
		char* markup = g_strdup_printf(
			"<span foreground=\"#cc2e71\">⬅</span> <b>%s</b> left this channel",
			safe_name);
		GtkWidget* label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(label), markup);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_wrap(GTK_LABEL(label), TRUE);
		gtk_widget_set_margin_start(label, 6);
		gtk_widget_set_margin_end(label, 6);
		gtk_widget_set_margin_top(label, 2);
		gtk_widget_set_margin_bottom(label, 2);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
		gtk_list_box_append(GTK_LIST_BOX(chatarea), row);
		g_free(markup);
		g_free(safe_name);
	}
	free(payload->channel_id);
	if (payload->user)
		FreeUserObject(payload->user);
	free(payload);
	return G_SOURCE_REMOVE;
}

void onYAMPChannelJoined(YampUser* user, char* channel_id) {
	if (!user || !channel_id)
		return;
	MainThreadCJLPayload* payload = malloc(sizeof(*payload));
	payload->channel_id = strdup(channel_id);
	payload->user = DupUserObject(user);
	g_idle_add((GSourceFunc)ChannelJoined_MainThread, payload);
}
void onYAMPChannelLeave(YampUser* user, char* channel_id) {
	if (!user || !channel_id)
		return;
	MainThreadCJLPayload* payload = malloc(sizeof(*payload));
	payload->channel_id = strdup(channel_id);
	payload->user = DupUserObject(user);
	g_idle_add((GSourceFunc)ChannelLeft_MainThread, payload);
}
GtkWidget* FindMembersRow(char* id) {
	GtkWidget* row = gtk_widget_get_first_child(suvbox);

	while (row) {
		GtkWidget* box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
		GtkWidget* label =
			gtk_widget_get_next_sibling(gtk_widget_get_first_child(box));

		char* rowid = g_object_get_data(G_OBJECT(label), "user-id");

		if (rowid && strcmp(rowid, id) == 0)
			return row;

		row = gtk_widget_get_next_sibling(row);
	}

	return NULL;
}
int ProfileUpdateMainThread(YampUser* new_profile) {
	if (strcmp(new_profile->id, curUserID) == 0) {
		if (new_profile->displayname &&
			strcmp(new_profile->displayname, new_profile->username) != 0) {
			gtk_widget_set_visible(UsernameLabel, 1);
			gtk_label_set_text(GTK_LABEL(UsernameLabel), new_profile->username);
			gtk_label_set_text(GTK_LABEL(DisplayNameLabel),
							   new_profile->displayname);
		} else {
			gtk_widget_set_visible(UsernameLabel, 0);
			gtk_label_set_text(GTK_LABEL(DisplayNameLabel),
							   new_profile->username);
		}


		if (new_profile->pfp && strlen(new_profile->pfp)) {
			char* url = ProcessAttachmentURL(new_profile->pfp);
			char* pfp_path = AvatarCachePath(url);
			LoadAvatarAsync(SelfPfp, url, pfp_path, 36);
			free(url);
			InsertPfpPath(new_profile->id, pfp_path);
		} else {
			char pfppath[37];
			sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
					GetDefaultPfp(new_profile->id));
			gtk_image_set_from_resource(GTK_IMAGE(SelfPfp), pfppath);
		}
	} else {
		if (listmode) {
			GtkWidget* wdg = FindConvRow(new_profile->id, YAMP_DM);
			if (wdg) {
				GtkWidget* box = gtk_widget_get_first_child(wdg);
				GtkWidget* statusFrame = gtk_widget_get_first_child(box);
				GtkWidget* pfp = gtk_widget_get_first_child(statusFrame);
				GtkWidget* label = gtk_widget_get_next_sibling(statusFrame);

				char* url = ProcessAttachmentURL(new_profile->pfp);
				char* pfp_path = AvatarCachePath(url);
				LoadAvatarAsync(pfp, url, pfp_path, 36);
				free(url);
				InsertPfpPath(new_profile->id, pfp_path);

				gtk_label_set_text(GTK_LABEL(label),
								   new_profile->displayname
									   ? new_profile->displayname
									   : new_profile->username);
			}
		}
	}

	if (MembersListShown) {
		GtkWidget* wdg = FindMembersRow(new_profile->id);
		if (wdg) {
			GtkWidget* box = gtk_widget_get_first_child(wdg);
			GtkWidget* statusFrame = gtk_widget_get_first_child(box);
			GtkWidget* pfp = gtk_widget_get_first_child(statusFrame);
			GtkWidget* label = gtk_widget_get_next_sibling(statusFrame);

			char* url = ProcessAttachmentURL(new_profile->pfp);
			char* pfp_path = AvatarCachePath(url);
			LoadAvatarAsync(pfp, url, pfp_path, 36);
			free(url);
			InsertPfpPath(new_profile->id, pfp_path);

			if (strcmp(new_profile->id, curUserID) == 0) {
				char* dpyname = new_profile->displayname
									? new_profile->displayname
									: new_profile->username;
				char dpyname_suffixed[strlen(dpyname) + 6];
				sprintf(dpyname_suffixed, "%s (you)", dpyname);
				gtk_label_set_text(GTK_LABEL(label), dpyname_suffixed);
			} else {
				gtk_label_set_text(GTK_LABEL(label),
								   new_profile->displayname
									   ? new_profile->displayname
									   : new_profile->username);
			}
		}
	}

	YampUser* usr = GetUserObject(new_profile->id);
	free(usr->displayname);
	usr->displayname = dup0(new_profile->displayname);
	free(usr->pfp);
	usr->pfp = dup0(new_profile->pfp);


	FreeUserObject(new_profile);
	return G_SOURCE_REMOVE;
}
void onYAMPProfileUpdated(YampUser* new_profile) {
	g_idle_add(G_SOURCE_FUNC(ProfileUpdateMainThread),
			   DupUserObject(new_profile));
}
typedef struct {
	YampUser* usr;
	char* channel;
} MemberEventPayload;
void MemberLeftMainThread(MemberEventPayload* pld) {
	YampChannel* channel = NULL;
	for (int i = 0; i < ncached_convs; i++) {
		if (strcmp(pld->channel, cached_convs[i]->id) == 0) {
			channel = cached_convs[i];
			break;
		}
	}
	if (!channel) {
		FreeUserObject(pld->usr);
		free(pld->channel);
		return;
	}

	for (int i = 0; i < channel->npeople; i++) {
		if (strcmp(channel->people[i]->id, pld->usr->id) == 0) {
			memmove(&channel->people[i], &channel->people[i + 1],
					sizeof(YampUser*) * (channel->npeople - i - 1));
			channel->npeople--;
			break;
		}
	}

	if (currentChannel && strcmp(currentChannel->id, pld->channel) == 0) {
		RemoveMember(pld->usr->id);
	}

	FreeUserObject(pld->usr);
	free(pld->channel);
}
void MemberJoinedMainThread(MemberEventPayload* pld) {
	char* owner_id = NULL;
	YampChannel* channel = NULL;
	for (int i = 0; i < ncached_convs; i++) {
		if (strcmp(pld->channel, cached_convs[i]->id) == 0) {
			channel = cached_convs[i];
			owner_id = cached_convs[i]->owner;
			break;
		}
	}
	if (!channel) {
		FreeUserObject(pld->usr);
		free(pld->channel);
		return;
	}
	channel->people =
		realloc(channel->people, sizeof(YampUser*) * (channel->npeople + 1));
	channel->people[channel->npeople] = DupUserObject(pld->usr);
	InsertUserObject(pld->usr->id, channel->people[channel->npeople]);
	channel->npeople++;
	if (strcmp(currentChannel->id, pld->channel) == 0) {
		AppendMember(pld->usr, curUserID);
	}
	FreeUserObject(pld->usr);
	free(pld->channel);
}
void onYAMPMemberJoined(YampUser* user, char* channel) {
	MemberEventPayload* payload = malloc(sizeof(*payload));
	payload->channel = strdup(channel);
	payload->usr = DupUserObject(user);
	g_idle_add(G_SOURCE_FUNC(MemberJoinedMainThread), payload);
}
void onYAMPMemberLeft(YampUser* user, char* channel) {
	MemberEventPayload* payload = malloc(sizeof(*payload));
	payload->channel = strdup(channel);
	payload->usr = DupUserObject(user);
	g_idle_add(G_SOURCE_FUNC(MemberLeftMainThread), payload);
}

static YampEventListener yamp_listener = {
	.onReceiveIM = onYAMPReceiveIM,
	.onStatusUpdate = onYAMPStatusUpdate,
	.onFriendRequestReceived = onYAMPFriendRequestReceived,
	.onNewSpace = onYAMPNewSpace,
	.onNewFriend = onYAMPNewFriend,
	.onNewChannel = onYAMPNewChannel,
	.onChannelUpdated = onYAMPChannelUpdated,
	.onChannelDeleted = onYAMPChannelDeleted,
	.onTyping = onYAMPTyping,
	.onMessageDeleted = onYAMPMessageDeleted,
	.onChannelJoined = onYAMPChannelJoined,
	.onChannelLeave = onYAMPChannelLeave,
	.onDisconnected = onYAMPDisconnected,
	.onProfileUpdated = onYAMPProfileUpdated,
	.onMemberJoined = onYAMPMemberJoined,
	.onMemberLeft = onYAMPMemberLeft,
};

void StartMainIMWindow(const YampLoginData* dat) {
	YAMPRegisterEventListener(&yamp_listener);
	InitMsgMenus();
	YampUser* heapusr = DupUserObject(dat->usr);

	strcpy(heapusr->id, dat->usr->id);
	InsertUserObject(heapusr->id, heapusr);
	char* displayName = dat->usr->displayname;
	if (!displayName) {
		displayName = dat->usr->username;
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
	BuildUserProfile(suvbox, dat->usr);
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
	gtk_widget_set_size_request(dmsbutton, 48, 48);
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
	gtk_image_set_pixel_size(GTK_IMAGE(dmsbtnimg), 26);
	gtk_button_set_child(GTK_BUTTON(dmsbutton), dmsbtnimg);
	g_signal_connect(dmsbutton, "clicked", G_CALLBACK(on_dms_btn_clicked),
					 NULL);

	gtk_widget_add_css_class(dmsbutton, "space_button");

	GtkWidget* dms_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_halign(dms_sep, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(dms_sep, 40, -1);

	gtk_box_append(GTK_BOX(GuildVBOX), dmsbutton);
	gtk_box_append(GTK_BOX(GuildVBOX), dms_sep);

	gtk_box_append(GTK_BOX(GuildVBOX), GuildList);
	gtk_widget_set_vexpand(GuildList, 1);

	GtkWidget* insertbtn = gtk_button_new();
	g_signal_connect(insertbtn, "clicked", G_CALLBACK(cb_insert_space), NULL);
	gtk_widget_set_halign(insertbtn, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(insertbtn, GTK_ALIGN_END);
	gtk_widget_set_size_request(insertbtn, 48, 48);

	GtkWidget* insertbtnimg = gtk_image_new_from_icon_name("insert-symbolic");
	gtk_widget_add_css_class(insertbtnimg, "active");
	gtk_image_set_pixel_size(GTK_IMAGE(insertbtnimg), 26);
	gtk_button_set_child(GTK_BUTTON(insertbtn), insertbtnimg);

	gtk_widget_add_css_class(insertbtn, "space_button");

	gtk_box_append(GTK_BOX(GuildVBOX), insertbtn);

	gtk_box_append(GTK_BOX(hbox), GuildVBOX);

	gtk_overlay_set_child(GTK_OVERLAY(notifoverlay), panelhbox);
	gtk_box_append(GTK_BOX(panelhbox), vbox);
	gtk_box_append(GTK_BOX(vbox), hbox);

	ChannelSidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_vexpand(ChannelSidebar, TRUE);
	gtk_widget_set_size_request(ChannelSidebar, 240, -1);

	gtk_box_append(GTK_BOX(hbox), ChannelSidebar);
	gtk_box_append(GTK_BOX(panelhbox), stackpane);

	chattitle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	topbar_group = gtk_size_group_new(GTK_SIZE_GROUP_VERTICAL);
	gtk_widget_set_size_request(chattitle, -1, TOPBAR_HEIGHT);
	gtk_widget_set_valign(chattitle, GTK_ALIGN_START);
	gtk_size_group_add_widget(topbar_group, chattitle);
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
	typingindicator = gtk_label_new(NULL);
	gtk_widget_set_visible(typingindicator, 0);
	replyindicator = gtk_label_new(NULL);
	gtk_widget_set_visible(replyindicator, 0);
	AttachementsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_visible(AttachementsBox, 0);
	gtk_box_append(GTK_BOX(chatvbox), typingindicator);
	gtk_box_append(GTK_BOX(chatvbox), replyindicator);
	gtk_box_append(GTK_BOX(chatvbox), AttachementsBox);
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
	entrydat->emoji = calloc(1, sizeof(EmojiPayload));
	GtkEventController* key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(EntryKeyHandler),
					 entrydat);
	gtk_widget_add_controller(entry, GTK_EVENT_CONTROLLER(key_controller));

	if (dat->usr->pfp && strlen(dat->usr->pfp)) {
		char* url = ProcessAttachmentURL(dat->usr->pfp);
		char* pfp_path = AvatarCachePath(url);
		SelfPfp = gtk_image_new();
		LoadAvatarAsync(SelfPfp, url, pfp_path, 36);
		free(url);
		InsertPfpPath(dat->usr->id, pfp_path);
	} else {
		char pfppath[37];
		sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
				GetDefaultPfp(dat->usr->id));
		SelfPfp = gtk_image_new_from_resource(pfppath);
	}
	gtk_widget_add_css_class(SelfPfp, "avatar");
	SelfPfpFrame = WrapInStatusFrame(SelfPfp, 36, "108020");

	GtkWidget* UserDetailsArea = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(UserDetailsArea, 0, 50);
	GtkWidget* UserDetailsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_hexpand(UserDetailsBox, 1);
	gtk_widget_set_valign(SelfPfpFrame, GTK_ALIGN_CENTER);
	gtk_box_append(GTK_BOX(UserDetailsBox), SelfPfpFrame);
	// the user logs in as online by default so this is fixed
	// change when that statement changes
	GtkWidget* UsernameBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(UsernameBox, GTK_ALIGN_CENTER);
	DisplayNameLabel = gtk_label_new(displayName);
	gtk_box_append(GTK_BOX(UserDetailsBox), UsernameBox);
	gtk_box_append(GTK_BOX(UsernameBox), DisplayNameLabel);
	if (dat->usr->displayname &&
		strcmp(dat->usr->displayname, dat->usr->username) != 0) {
		UsernameLabel = gtk_label_new(dat->usr->username);
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
	for (int i = 0; i < dat->nspaces; i++) {
		InsertSpace(dat->spaces[i]);
	}
	// show notifications per every new friend request
	for (int i = 0; i < dat->fqcount; i++) {
		CreateFQNotification(dat->incfq[i]);
	}
	gtk_window_set_child(GTK_WINDOW(main_window), notifoverlay);
	on_dms_btn_clicked(GTK_BUTTON(dmsbutton), NULL);
}