#include <stdio.h>
#include <gtk/gtk.h>
#include "imwnd.h"
#include "gdk/gdkkeysyms.h"
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
#include "insertspace.h"
#include "notification.h"
#include "usersettings.h"
#include "gcmemselect.h"
#include "emoji_map.h"
#include "chatinput.h"
#include "types.h"
#include "invitedialog.h"

void onYAMPDisconnected(void);

/* Spaces */
void onYAMPSpacesFetched(YampSpace* spaces, int nspaces);
void onYAMPNewSpace(YampSpace space);

/* Channels */
void onYAMPChannelsFetched(YampChannel* channels, int nchannels);
void onYAMPChannelsUpdated(YampChannel* channels, int n, char* space);
void onYAMPChannelJoined(YampUser* user, char* channel_id);
void onYAMPChannelLeave(YampUser* user, char* channel_id);

/* Messages */
void onYAMPMessageHistoryLoaded(YampMessage** msgs, int nmsgs, int hasmore);
void onYAMPReceiveIM(YampMessage* msg, int nonce);
void onYAMPMessageDeleted(char* message_id);

/* Status / typing */
void onYAMPStatusUpdate(char* id, YampStatus* stat);
void onYAMPTyping(YampUser* usr, char* channel);

/* User details */
void onYAMPUserDetailsFetched(cJSON* Details);

/* Friends / conversations */
void onYAMPFriendRequestReceived(char* username);
void onYAMPFriendReqSent(int success);
void onYAMPFriendReqResolved(char* username, int success);
void onYAMPNewFriend(YampUser* friend);
void onYAMPNewConversation(YampChannel* conv);
void onYAMPFriendsListed(YampUser* friends, int nfriends);


char* curUserID;
GQueue MessageStack = G_QUEUE_INIT;
GtkWidget* main_window;
GtkWidget* ChannelSidebar;
GtkWidget* dmsbutton;
GtkWidget* GuildVBOX;
GtkWidget* GuildList;
GtkWidget* SelfPfp;
GtkWidget* SelfPfpFrame;
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
GtkWidget* typingindicator;
GtkWidget* replyindicator;
GtkWidget* AttachementsBox;
GtkWidget* memberlist = NULL;

YampChannel* currentChannel = NULL;
int isReplying = 0;
char* ReplyingToMsg = NULL;

typedef struct {
	char* id;
	char* name;
	guint timeout; // GSource id, 0 if already fired
} TypingEntry;

static GPtrArray* typing_users = NULL;
void TypingClear(void);

#define TYPING_TIMEOUT_SECONDS 8

gboolean listmode = FALSE;
char* curSpace = NULL;
char* curSpaceDisplay = NULL;
CURL* curl;
char* pfp_dir;
int MembersListShown = 0;

AttachementEntry* curAttachements = NULL;

#define TOPBAR_HEIGHT 40
GtkSizeGroup* topbar_group;

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

YampChannel* cached_convs = NULL;
int ncached_convs = 0;
YampUser** cached_friends = NULL;
int ncached_friends = 0;

void onYAMPDisconnected() { g_idle_add(onUIDisconnected, 0); }
#define AVATAR_MAX_BYTES (8 * 1024 * 1024)

typedef struct {
	char* url;
	char* cache_path;
	int px;
	GWeakRef widget;
	GBytes* bytes;
} AvatarFetch;

size_t AvatarWriteCb(char* ptr, size_t size, size_t nmemb, void* ud) {
	GByteArray* buf = ud;
	size_t n = size * nmemb;
	if (buf->len + n > AVATAR_MAX_BYTES)
		return 0;
	g_byte_array_append(buf, (guint8*)ptr, n);
	return n;
}
char* AvatarCachePath(const char* url) {
	char* hash = g_compute_checksum_for_string(G_CHECKSUM_SHA1, url, -1);
	char* path = g_build_filename(pfp_dir, hash, NULL);
	g_free(hash);
	return path;
}
gboolean AvatarDone(gpointer data) {
	AvatarFetch* f = data;
	GtkWidget* w = g_weak_ref_get(&f->widget);

	if (w) {
		if (f->bytes) {
			GError* err = NULL;
			GdkTexture* tex = gdk_texture_new_from_bytes(f->bytes, &err);
			if (tex) {
				gtk_image_set_from_paintable(GTK_IMAGE(w), GDK_PAINTABLE(tex));
				if (f->px > 0)
					gtk_image_set_pixel_size(GTK_IMAGE(w), f->px);
				g_object_unref(tex);
			} else {
				g_warning("Bad avatar image from %s: %s", f->url, err->message);
				g_clear_error(&err);
			}
		}
		g_object_unref(w);
	}

	if (f->bytes)
		g_bytes_unref(f->bytes);
	g_weak_ref_clear(&f->widget);
	g_free(f->url);
	g_free(f->cache_path);
	g_free(f);
	return G_SOURCE_REMOVE;
}

gpointer AvatarThread(gpointer data) {
	AvatarFetch* f = data;
	GByteArray* buf = g_byte_array_new();
	gboolean from_cache = FALSE;

	if (f->cache_path) {
		gchar* contents = NULL;
		gsize len = 0;
		if (g_file_get_contents(f->cache_path, &contents, &len, NULL)) {
			if (len > 0 && len <= AVATAR_MAX_BYTES) {
				g_byte_array_append(buf, (guint8*)contents, len);
				from_cache = TRUE;
			}
			g_free(contents);
		}
	}

	if (!from_cache) {
		CURL* c = curl_easy_init();
		if (c) {
			curl_easy_setopt(c, CURLOPT_URL, f->url);
			curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, AvatarWriteCb);
			curl_easy_setopt(c, CURLOPT_WRITEDATA, buf);
			curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
			curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
			CURLcode rc = curl_easy_perform(c);
			curl_easy_cleanup(c);
			if (rc != CURLE_OK) {
				g_warning("Avatar fetch failed (%s): %s", f->url,
						  curl_easy_strerror(rc));
				g_byte_array_set_size(buf, 0);
			}
		}
		if (buf->len && f->cache_path)
			g_file_set_contents(f->cache_path, (gchar*)buf->data, buf->len,
								NULL);
	}

	if (buf->len)
		f->bytes = g_byte_array_free_to_bytes(buf);
	else
		g_byte_array_free(buf, TRUE);

	g_idle_add(AvatarDone, f);
	return NULL;
}
void LoadAvatarAsync(GtkWidget* image, const char* url, const char* cache_path,
					 int px) {
	if (!image || !url || !*url)
		return;
	AvatarFetch* f = g_new0(AvatarFetch, 1);
	f->url = g_strdup(url);
	f->cache_path = g_strdup(cache_path);
	f->px = px;
	g_weak_ref_init(&f->widget, image);
	g_thread_unref(g_thread_new("avatar-fetch", AvatarThread, f));
}



static void SetStatusFrameColor(GtkWidget* frame, const char* hex) {
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

void BuildUserProfile(YampUser* usr) {
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
	if (usr->pfp && strlen(usr->pfp)) {
		pfp = gtk_image_new();
		char* url = ProcessAttachmentURL(usr->pfp);
		char* pfp_path = AvatarCachePath(url);
		LoadAvatarAsync(pfp, url, pfp_path, 80);
		InsertPfpPath(usr->id, pfp_path);
		free(url);
		g_free(pfp_path);
	} else {
		char pfppath[38];
		sprintf(pfppath, "/org/yampen/assets/pfps/default%d.png",
				GetDefaultPfp(usr->id));
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
	if (usr->displayname) {
		GtkWidget* dpyname = gtk_label_new(usr->displayname);
		gtk_widget_add_css_class(dpyname, "h2");
		gtk_box_append(GTK_BOX(infoarea), dpyname);
		GtkWidget* usrname = gtk_label_new(usr->username);
		gtk_label_set_xalign(GTK_LABEL(usrname), 0.0f);
		gtk_box_append(GTK_BOX(infoarea), usrname);
	} else {
		GtkWidget* usrname = gtk_label_new(usr->username);
		gtk_widget_add_css_class(usrname, "h2");
		gtk_box_append(GTK_BOX(infoarea), usrname);
	}
	gtk_box_append(GTK_BOX(suvbox), overlay);
	gtk_box_append(GTK_BOX(suvbox), infoarea);
}
static const char* StatusToColor(const char* statusStr);

typedef struct {
	YampUser** users;
	int n;
} MemberSet;

MemberSet* MemberSetCopy(YampUser** src, int n) {
	MemberSet* set = malloc(sizeof(MemberSet));
	set->n = n;
	set->users = calloc(n ? n : 1, sizeof(YampUser));
	for (int i = 0; i < n; i++) {
		set->users[i] = DupUserObject(src[i]);
	}
	return set;
}

void MemberSetFree(gpointer data) {
	MemberSet* set = data;
	if (!set)
		return;
	for (int i = 0; i < set->n; i++) {
		FreeUserObject(set->users[i]);
	}
	free(set->users);
	free(set);
}

void BuildMemberList(MemberSet* set) {
	char* hdrtxt = g_strdup_printf("Members - %d", set->n);
	GtkWidget* hdr = gtk_label_new(hdrtxt);
	g_free(hdrtxt);
	gtk_widget_add_css_class(hdr, "channel-category");
	gtk_widget_set_halign(hdr, GTK_ALIGN_START);
	gtk_widget_set_margin_start(hdr, 16);
	gtk_widget_set_margin_top(hdr, 12);
	gtk_widget_set_margin_bottom(hdr, 4);
	gtk_box_append(GTK_BOX(suvbox), hdr);

	GtkWidget* list = gtk_list_box_new();
	memberlist = list;
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);

	for (int i = 0; i < set->n; i++) {
		const YampUser* u = set->users[i];
		const char* name = u->displayname ? u->displayname : u->username;

		GtkWidget* row = gtk_list_box_row_new();
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_margin_start(box, 8);
		gtk_widget_set_margin_end(box, 8);
		gtk_widget_set_margin_top(box, 4);
		gtk_widget_set_margin_bottom(box, 4);


		GtkWidget* pfp;
		if (u->pfp && *u->pfp) {
			pfp = gtk_image_new();
			char* url = ProcessAttachmentURL(u->pfp);
			char* pfp_path = AvatarCachePath(url);
			LoadAvatarAsync(pfp, url, pfp_path, 32);
			InsertPfpPath(u->id, pfp_path);
			free(url);
			g_free(pfp_path);
		} else {
			char* respath = g_strdup_printf(
				"/org/yampen/assets/pfps/default%d.png", GetDefaultPfp(u->id));
			pfp = gtk_image_new_from_resource(respath);
			g_free(respath);
		}
		gtk_widget_add_css_class(pfp, "avatar");

		const char* stat = u->status->status ? u->status->status : "offline";
		GtkWidget* frame = WrapInStatusFrame(pfp, 32, StatusToColor(stat));
		g_object_set_data_full(G_OBJECT(frame), "user-id", g_strdup(u->id),
							   g_free);
		char* label_txt = (curUserID && strcmp(u->id, curUserID) == 0)
							  ? g_strdup_printf("%s (you)", name)
							  : g_strdup(name);
		GtkWidget* lbl = gtk_label_new(label_txt);
		g_free(label_txt);
		gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
		gtk_widget_set_halign(lbl, GTK_ALIGN_START);
		gtk_widget_set_hexpand(lbl, TRUE);

		gtk_box_append(GTK_BOX(box), frame);
		gtk_box_append(GTK_BOX(box), lbl);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
		gtk_list_box_append(GTK_LIST_BOX(list), row);
	}

	GtkWidget* scroller = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
								   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_box_append(GTK_BOX(suvbox), scroller);
}
void OnDMRowSelected(GtkListBox* box, GtkListBoxRow* row, gpointer user_data) {
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
	GtkWidget* child = gtk_widget_get_next_sibling(
		gtk_widget_get_first_child(gtk_list_box_row_get_child(row)));
	currentChannel = g_object_get_data(G_OBJECT(child), "channel");
	int type = (int)g_object_get_data(G_OBJECT(child), "type");
	LastTypedAt = 0;
	if (type == YAMP_DM) {
		memberlist = NULL;
		BuildUserProfile(
			GetUserObject(g_object_get_data(G_OBJECT(child), "id")));
		MembersListShown = 0;
		char* name = gtk_label_get_text(GTK_LABEL(child));
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
		char* name = gtk_label_get_text(GTK_LABEL(child));
		GtkWidget* titlewdg;
		while ((titlewdg = gtk_widget_get_first_child(chattitle)) != NULL)
			gtk_widget_unparent(titlewdg);

		MembersListShown = 1;

		GtkWidget* titlebox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_margin_start(titlebox, 10);
		gtk_widget_set_valign(chattitle, GTK_ALIGN_CENTER);
		char* dpyname = g_object_get_data(G_OBJECT(child), "dpyname");
		GtkWidget* recipientname = gtk_label_new(dpyname);
		gtk_box_append(GTK_BOX(titlebox), recipientname);
		gtk_box_append(GTK_BOX(chattitle), titlebox);
		MemberSet* members = g_object_get_data(G_OBJECT(child), "members");
		if (members) {
			BuildMemberList(members);
		}
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
	currentChannel =
		((YampChannel*)g_object_get_data(G_OBJECT(row), "channel"));
	YAMPJoinChannel(mainsock, currentChannel->id, NULL);
	YAMPGetMessageHistory(mainsock, currentChannel->id, 50, NULL,
						  onYAMPMessageHistoryLoaded);
}
void SpaceRowCB(GtkButton* btn, gpointer user_data) {
	gtk_widget_remove_css_class(dmsbutton, "active");
	gtk_widget_remove_css_class(selectedspacebtn, "active");
	gtk_widget_add_css_class(GTK_WIDGET(btn), "active");
	selectedspacebtn = GTK_WIDGET(btn);
	listmode = FALSE;
	YampSpace* space = user_data;
	curSpace = space->id;

	YAMPListSpaceChannels(mainsock, curSpace, onYAMPChannelsFetched);
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
	*heapspace = DupSpaceObject(&space);
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
	g_menu_append(menu, "Create Channel", "channelmenu.create-channel");
	GMenu* catmenu = g_menu_new();
	g_menu_append(menu, "Create Category", "channelmenu.create-category");

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
	gtk_widget_set_halign(GTK_WIDGET(popover), GTK_ALIGN_START);
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
		gboolean is_category = ch->type == YAMP_SPACE_CATEGORY;

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

			YampChannel* chdup = DupChannelObject(ch);
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
static void OnInviteBtnClicked(GtkButton* btn, gpointer data) {
	if (!curSpace)
		return;
	YampSpace* sp = GetSpaceObject(curSpace);
	SpawnInviteDialog(GTK_WINDOW(main_window), curSpace,
					  sp ? sp->displayname : NULL);
}
int MainThreadChannelCB(gpointer data) {
	ChannelFetchPayload* cfp = data;
	GtkWidget* child;

	while ((child = gtk_widget_get_first_child(ChannelSidebar)) != NULL)
		gtk_widget_unparent(child);
	GtkWidget* headerbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(headerbox, -1, TOPBAR_HEIGHT);
	gtk_widget_set_valign(headerbox, GTK_ALIGN_START);
	GtkWidget* headerlbl = gtk_label_new(GetSpaceObject(curSpace)->displayname);
	gtk_widget_set_size_request(headerlbl, 180, -1);
	gtk_label_set_ellipsize(GTK_LABEL(headerlbl), PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(GTK_LABEL(headerlbl),
						 0.0); // keep text left-aligned as it shrinks48
	gtk_widget_set_hexpand(headerlbl, FALSE);
	gtk_box_append(GTK_BOX(headerbox), headerlbl);

	GtkWidget* invbtn = gtk_button_new();
	g_signal_connect(invbtn, "clicked", G_CALLBACK(OnInviteBtnClicked), NULL);
	GtkWidget* invimg = gtk_image_new_from_icon_name("invite-symbolic");
	gtk_button_set_child(GTK_BUTTON(invbtn), invimg);
	gtk_box_append(GTK_BOX(headerbox), invbtn);
	gtk_size_group_add_widget(topbar_group, headerbox);
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
	if (!curSpace || !space || strcmp(space, curSpace) != 0)
		return;
	ChannelFetchPayload* cfp = malloc(sizeof(ChannelFetchPayload));
	cfp->nchannels = n;
	cfp->channels = malloc(sizeof(YampChannel) * (n ? n : 1));
	for (int i = 0; i < n; i++)
		cfp->channels[i] = DupChannelObject_Inline(&channels[i]);
	g_idle_add(MainThreadChannelCB, cfp);
}
typedef struct {
	char* id;
	YampStatus* stat;
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
	const char* st = p->stat->status ? p->stat->status : "offline";
	const char* color = StatusToColor(st);

	YampUser* u = GetUserObject(p->id);
	if (u) {
		if (u->status) FreeStatusObject(u->status);
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
	gtk_stack_set_visible_child_name(GTK_STACK(stackpane), "friends");
}

char* JoinUsernames(YampUser** people, int n) {
	size_t len = 1;
	for (int i = 0; i < n; i++) {
		const char* nm = people[i]->displayname ? people[i]->displayname
												: people[i]->username;
		len += strlen(nm) + (i ? 2 : 0);
	}

	char* out = malloc(len);
	if (!out)
		return NULL;

	char* p = out;
	for (int i = 0; i < n; i++) {
		const char* nm = people[i]->displayname ? people[i]->displayname
												: people[i]->username;
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
		YampUser** members = conv.people;
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
		g_object_set_data(G_OBJECT(LBRowLabel), "id", heapusr->id);
		g_object_set_data(G_OBJECT(LBRowLabel), "channel",
						  DupChannelObject(&conv));
		g_object_set_data_full(G_OBJECT(LBRowLabel), "dpyname",
							   g_strdup(dispname), g_free);
		g_object_set_data(G_OBJECT(LBRowLabel), "type", (void*)conv.type);
		gtk_box_append(GTK_BOX(ItemBox), statusframe);
		gtk_box_append(GTK_BOX(ItemBox), LBRowLabel);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(LBRow), ItemBox);
		gtk_widget_set_halign(LBRowLabel, GTK_ALIGN_START);
		gtk_list_box_append(GTK_LIST_BOX(dmlist), LBRow);
	} else if (conv.type == YAMP_GC) {
		YampUser** members = conv.people;
		char* dispname =
			conv.name ? conv.name : JoinUsernames(members, conv.npeople);
		GtkWidget* ItemBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		GtkWidget* LBRow = gtk_list_box_row_new();
		GtkWidget* LBRowLabel = gtk_label_new(dispname);
		GtkWidget* Pfp;
		char pfppath[37];
		sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
				GetDefaultPfp(conv.id));
		Pfp = gtk_image_new_from_resource(pfppath);

		GtkCssProvider* provider = gtk_css_provider_new();
		char style[60];
		sprintf(style, "image { -gtk-icon-size: 32px; }");
		gtk_css_provider_load_from_string(provider, style);
		gtk_style_context_add_provider(gtk_widget_get_style_context(Pfp),
									   GTK_STYLE_PROVIDER(provider),
									   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_set_data(G_OBJECT(LBRowLabel), "channel",
						  DupChannelObject(&conv));
		g_object_set_data(G_OBJECT(LBRowLabel), "id",
						  strdup(ParseGCChannel(conv.id)));
		g_object_set_data(G_OBJECT(LBRowLabel), "type", (void*)conv.type);
		g_object_set_data_full(G_OBJECT(LBRowLabel), "members",
							   MemberSetCopy(conv.people, conv.npeople),
							   MemberSetFree);
		g_object_set_data(G_OBJECT(LBRowLabel), "dpyname", dispname);
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
	YAMPCreateGC(mainsock, initmembers, nusers, NULL);
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
	YAMPSendFriendReq(
		mainsock,
		gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(data))), NULL);
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

static void FQPayloadFree(fqnotifpayload* p) {
	free(p->username);
	free(p);
}

void AcceptFQBtnCB(GtkButton* btn, void* data) {
	fqnotifpayload* payload = data;
	YAMPAcceptFriendReq(mainsock, payload->username, NULL);
	YAMPStartDM(mainsock, payload->username, NULL);
	gtk_widget_unparent(payload->notif);
	FQPayloadFree(payload);
}
void DenyFQBtnCB(GtkButton* btn, void* data) {
	fqnotifpayload* payload = data;
	YAMPDenyFriendReq(mainsock, payload->username, NULL);
	gtk_widget_unparent(payload->notif);
	FQPayloadFree(payload);
}

int CreateFQNotification(YampUser* usr) {
	char* txt = malloc(25 + strlen(usr->displayname));
	fqnotifpayload* payload = malloc(sizeof(fqnotifpayload));
	payload->username = strdup(usr->username);
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
}
void onYAMPFriendRequestReceived(char* id) {
	YampUser usr;
	usr.displayname = id;
	usr.username = id;
	strcpy(usr.id, id);
	CreateFQNotification(&usr);
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
void onYAMPNewFriend(YampUser* friend) {
	YampUser* heapfriend = DupUserObject(friend);
	g_idle_add(MainThreadNewFriend, heapfriend);
}
gboolean MainThreadNewConversation(YampChannel* heapconv) {
	InsertConvIntoList(*heapconv);
	free(heapconv);
	return G_SOURCE_REMOVE;
}
void onYAMPNewConversation(YampChannel* conv) {
	YampChannel* heapconv = DupChannelObject(conv);
	g_idle_add(G_SOURCE_FUNC(MainThreadNewConversation), heapconv);
}
void onYAMPFriendsListed(YampUser* friends, int nfriends) {
	MainThreadFriendPayload* payload = malloc(sizeof(MainThreadFriendPayload));
	payload->users = friends;
	payload->nusers = nfriends;
	g_idle_add(MainThreadFriendCB, payload);
}
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
		char* id;
		YampUser* usr = payload->msg->author;
		id = usr->id;
		if (!usr) {
			return G_SOURCE_REMOVE;
		}

		TypingRemove(usr->id);

		char* dispname = usr->displayname ? usr->displayname : usr->username;
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
		char* id;
		YampUser* usr = payload->msgs[i]->author;
		id = usr->id;
		if (!usr) {
			return G_SOURCE_REMOVE;
		}
		char* dispname = usr->displayname ? usr->displayname : usr->username;
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
GtkWidget* FindConvRow(char* id, int type) {
	GtkWidget* row = gtk_widget_get_first_child(dmlist);

	while (row) {
		GtkWidget* box = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(row));
		GtkWidget* label =
			gtk_widget_get_next_sibling(gtk_widget_get_first_child(box));

		int rowtype =
			GPOINTER_TO_INT(g_object_get_data(G_OBJECT(label), "type"));

		if (rowtype == type) {
			char* rowid = g_object_get_data(G_OBJECT(label), "id");

			if (rowid && strcmp(rowid, id) == 0)
				return row;
		}

		row = gtk_widget_get_next_sibling(row);
	}

	return NULL;
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
	usr->displayname = strdup(new_profile->displayname);
	free(usr->pfp);
	usr->pfp = strdup(new_profile->pfp);


	FreeUserObject(new_profile);
	return G_SOURCE_REMOVE;
}
void onYAMPProfileUpdated(YampUser* new_profile) {
	g_idle_add(G_SOURCE_FUNC(ProfileUpdateMainThread),
			   DupUserObject(new_profile));
}

static YampEventListener yamp_listener = {
	.onReceiveIM = onYAMPReceiveIM,
	.onStatusUpdate = onYAMPStatusUpdate,
	.onFriendRequestReceived = onYAMPFriendRequestReceived,
	.onChannelsUpdated = onYAMPChannelsUpdated,
	.onNewSpace = onYAMPNewSpace,
	.onNewFriend = onYAMPNewFriend,
	.onNewConversation = onYAMPNewConversation,
	.onTyping = onYAMPTyping,
	.onMessageDeleted = onYAMPMessageDeleted,
	.onChannelJoined = onYAMPChannelJoined,
	.onChannelLeave = onYAMPChannelLeave,
	.onDisconnected = onYAMPDisconnected,
	.onProfileUpdated = onYAMPProfileUpdated,
};

void StartMainIMWindow(const YampLoginData* dat) {
	YAMPRegisterEventListener(&yamp_listener);
	InitMsgMenus();
	YampUser* heapusr = DupUserObject(dat->usr);

	printf("%s\n", dat->usr->id);
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

	chattitle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	topbar_group = gtk_size_group_new(GTK_SIZE_GROUP_VERTICAL);
	chattitle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
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
	LoadSpaces(dat->spaces, dat->nspaces);
	for (int i = 0; i < dat->fqcount; i++) {
		CreateFQNotification(dat->incfq[i]);
	}
	gtk_window_set_child(GTK_WINDOW(main_window), notifoverlay);
	on_dms_btn_clicked(GTK_BUTTON(dmsbutton), NULL);
}