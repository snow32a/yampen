#include "globals.h"
#include "gtk/gtk.h"
#include "hashtables.h"
#include "protocol/yamp.h"
typedef struct {
	char *where;
	GtkWidget *EntryArea;
	GtkWidget *ChatView;
} send_im_obj;
void PushUIMessage(GtkWidget *chatarea, char *username, char *content) {
	GtkWidget *msgrow = gtk_list_box_row_new();
	gtk_widget_set_hexpand(msgrow, TRUE);
	gtk_widget_set_halign(msgrow, GTK_ALIGN_START);
	GtkWidget *msghbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_halign(msghbox, GTK_ALIGN_START);
	gtk_widget_set_hexpand(msghbox, TRUE);
	GtkWidget *msgvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *usrtext = gtk_label_new(username);
	gtk_label_set_markup(GTK_LABEL(usrtext),
	                     g_strdup_printf("<b>%s</b>", username));
	gtk_widget_set_halign(usrtext, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(msgvbox), usrtext);
	GtkWidget *msgtext = gtk_label_new(content);
	GtkCssProvider *msgbubprovider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(msgbubprovider,
									  "label { padding: 10px; border-radius: 10px; background-color: rgba(0, 0, 0, 0.5); }");

	GtkStyleContext *msgbubcontext = gtk_widget_get_style_context(msgtext);
	gtk_style_context_add_provider(
		msgbubcontext,
		GTK_STYLE_PROVIDER(msgbubprovider),
								   GTK_STYLE_PROVIDER_PRIORITY_USER
	);
	gtk_widget_set_halign(msgtext, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(msgvbox), msgtext);
	GtkWidget *userpfp = gtk_image_new_from_file("pfp.png");
	gtk_image_set_pixel_size(GTK_IMAGE(userpfp), 36);
	gtk_box_append(GTK_BOX(msghbox), userpfp);
	gtk_box_append(GTK_BOX(msghbox), msgvbox);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(msgrow), msghbox);
	gtk_list_box_append(GTK_LIST_BOX(chatarea), msgrow);

	GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(
	    GTK_SCROLLED_WINDOW(g_object_get_data(
	        G_OBJECT(gtk_widget_get_ancestor(chatarea, GTK_TYPE_WINDOW)),
	        "scroll")));
	gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
}
static gboolean gui_send_im(GtkEventControllerKey *controller, guint keyval,
                            guint keycode, GdkModifierType state,
                            gpointer user_data) {
	if (keyval == GDK_KEY_KP_Enter) {
		send_im_obj *dat = (send_im_obj *)user_data;
		GtkTextBuffer *buf =
		    gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea));
		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter(buf, &start);
		gtk_text_buffer_get_end_iter(buf, &end);
		char *content = gtk_text_buffer_get_text(
		    gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea)), &start,
		    &end, TRUE);
		YAMPSendIM(mainsock, dat->where, content);
		gtk_text_buffer_set_text(
		    gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea)), "", 0);
		return TRUE;
	}
	return FALSE;
}
gboolean ChatWindowClose(gpointer data) {
	DeregisterChatWindow((char *)data);
	return FALSE;
}
void SpawnChatWindow(char *toWho) {
	GtkWidget *mainhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	if (GetChatWindow(toWho)) {
		return;
	}
	chat WhereParsed;
	if (!YAMPProcessWhere(toWho, curUsername, &WhereParsed)) {
		printf("malformed where, %s, returning\n",
		toWho);
		return;
	}
	if (WhereParsed.type == YAMP_GUILD) {
		printf("spawning a guild chat window at #%s\n",
		       WhereParsed.ChannelName);
	} else if (WhereParsed.type == YAMP_DM) {
		printf("spawning a DMs chat window for %s\n", WhereParsed.OtherGuy);
	}
	GtkWidget *chat_window = gtk_application_window_new(global_app);

	// vertical arragning box thing
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_box_append(GTK_BOX(mainhbox), vbox);

	// le chat area
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll, TRUE);
	g_object_set_data(G_OBJECT(chat_window), "scroll", scroll);

	GtkWidget *chat_view = gtk_list_box_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), chat_view);
	g_object_set_data(G_OBJECT(chat_window), "chatview", chat_view);
	gtk_box_append(GTK_BOX(vbox), scroll);
	// input row
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(vbox), hbox);

	GtkWidget *upload_btn = gtk_button_new_with_label("+");
	gtk_box_append(GTK_BOX(hbox), upload_btn);
	GtkWidget *entry = gtk_text_view_new();
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_box_append(GTK_BOX(hbox), entry);

	gtk_window_set_child(GTK_WINDOW(chat_window), mainhbox);

	send_im_obj *dat = malloc(sizeof(send_im_obj));
	dat->EntryArea = entry;
	dat->ChatView = chat_view;
	dat->where = strdup(toWho);
	GtkEventController *key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(gui_send_im),
	                 dat);
	gtk_widget_add_controller(entry, key_controller);
	g_signal_connect(chat_window, "close-request", G_CALLBACK(ChatWindowClose),
	                 dat->where);

	if (WhereParsed.type == YAMP_DM) {
		char *window_title =
		malloc(6 + 3 + strlen(GetDisplayName(WhereParsed.OtherGuy)) + 1);
		sprintf(window_title, "Yampen - %s", GetDisplayName(WhereParsed.OtherGuy));
		gtk_window_set_title(GTK_WINDOW(chat_window), window_title);
		GtkWidget* detailsArea = gtk_box_new(GTK_ORIENTATION_VERTICAL,5);
		gtk_widget_set_hexpand(detailsArea, FALSE);
		gtk_widget_set_size_request(detailsArea,340,-1);
		GtkCssProvider *detProvider = gtk_css_provider_new();
		gtk_css_provider_load_from_string(detProvider,
										  "* { padding: 10px; background-color: rgba(0, 0, 0, 0.5); }");

		GtkStyleContext *detContext = gtk_widget_get_style_context(detailsArea);
		gtk_style_context_add_provider(
			detContext,
			GTK_STYLE_PROVIDER(detProvider),
									   GTK_STYLE_PROVIDER_PRIORITY_USER
		);
		GtkWidget* pfp = gtk_image_new_from_file(GetPfpPath(WhereParsed.OtherGuy));
		gtk_image_set_pixel_size(GTK_IMAGE(pfp),64);
		GtkWidget* dispnamelabel = gtk_label_new(GetDisplayName(WhereParsed.OtherGuy));
		gtk_box_append(GTK_BOX(detailsArea),pfp);
		gtk_box_append(GTK_BOX(detailsArea),dispnamelabel);
		if(GetProfileDescription(WhereParsed.OtherGuy)){
		GtkWidget* desclabel = gtk_label_new(GetProfileDescription(WhereParsed.OtherGuy));
		gtk_label_set_wrap_mode(GTK_LABEL(desclabel), PANGO_WRAP_WORD);
		gtk_label_set_wrap(GTK_LABEL(desclabel), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(desclabel), 30);
		gtk_widget_set_hexpand(desclabel, TRUE);
		GtkCssProvider *provider = gtk_css_provider_new();
		gtk_css_provider_load_from_string(provider,
										"label { padding: 10px; border-radius: 10px; background-color: rgba(0, 0, 0, 0.5); }");

		GtkStyleContext *context = gtk_widget_get_style_context(desclabel);
		gtk_style_context_add_provider(
			context,
			GTK_STYLE_PROVIDER(provider),
									   GTK_STYLE_PROVIDER_PRIORITY_USER
		);
		gtk_widget_set_halign(desclabel,GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(detailsArea),desclabel);
		}
		gtk_box_append(GTK_BOX(mainhbox),detailsArea);
	} else {
		char *window_title =
		malloc(15 + strlen(WhereParsed.GuildName) + strlen(WhereParsed.ChannelName) + 1);
		sprintf(window_title, "Yampen - %s - #%s", WhereParsed.GuildName, WhereParsed.ChannelName);
		YAMPGetMessageHistory(mainsock,toWho);
		gtk_window_set_title(GTK_WINDOW(chat_window), window_title);
	}
	gtk_window_set_default_size(GTK_WINDOW(chat_window), 1200, 720);
	RegisterChatWindow(chat_window, dat->where);

	gtk_window_present(GTK_WINDOW(chat_window));
}

typedef struct {
	char *username;
	char *data;
	char *where;
} IMReceivePayload;

static gboolean receive_im_main_thread(gpointer user_data) {
	IMReceivePayload *payload = user_data;

	GtkWidget *targetWnd = GetChatWindow(payload->where);
	if (!targetWnd) {
		SpawnChatWindow(payload->where);
		targetWnd = GetChatWindow(payload->where);
	}

	GtkWidget *chatarea =
	    GTK_WIDGET(g_object_get_data(G_OBJECT(targetWnd), "chatview"));
	char *username;
	username = GetDisplayName(payload->username);
	if (!username) {
		username = payload->username;
	}
	PushUIMessage(chatarea, username, payload->data);
	return G_SOURCE_REMOVE;
}

void onYAMPReceiveIM(char *username, char *where, char *data) {
	IMReceivePayload *payload = malloc(sizeof(IMReceivePayload));
	payload->username = strdup(username);
	payload->data = strdup(data);
	payload->where = strdup(where);
	g_idle_add(receive_im_main_thread, payload);
}
