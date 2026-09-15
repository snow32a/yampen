#include "gdk-pixbuf/gdk-pixbuf.h"
#include "globals.h"
#include "gtk/gtk.h"
#include "hashtables.h"
#include "protocol/yamp.h"
#include "message.h"
#include <curl/curl.h>
#include "hmap/hashmap.h"
typedef struct {
	char* where;
	GtkWidget* EntryArea;
	GtkWidget* ChatView;
} send_im_obj;
#define STYLE_NONE 0
#define STYLE_BOLD (1 << 0)
#define STYLE_ITALIC (1 << 1)
#define STYLE_UNDERLINE (1 << 2)
#define STYLE_LINK (1 << 3)
char* linkTo;

static void BuildMarkdownMarkup(MarkdownElement* node, GString* out,
								unsigned int style) {
	if (!node)
		return;

	unsigned int childStyle = style;

	switch (node->type) {
	case MARKDOWN_BOLD:
		childStyle |= STYLE_BOLD;
		break;

	case MARKDOWN_ITALIC:
		childStyle |= STYLE_ITALIC;
		break;

	case MARKDOWN_UNDERLINE:
		childStyle |= STYLE_UNDERLINE;
		break;

	case MARKDOWN_LINK: {
		char* url = g_markup_escape_text(node->content, -1);

		g_string_append(out, "<a href=\"");
		g_string_append(out, url);
		g_string_append(out, "\">");

		for (unsigned int i = 0; i < node->nChildren; i++)
			BuildMarkdownMarkup(node->children[i], out, childStyle);

		g_string_append(out, "</a>");

		g_free(url);
		return;
	}

	default:
		break;
	}

	gboolean openedBold = FALSE;
	gboolean openedItalic = FALSE;
	gboolean openedUnderline = FALSE;

	if ((childStyle & STYLE_BOLD) && !(style & STYLE_BOLD)) {
		g_string_append(out, "<b>");
		openedBold = TRUE;
	}

	if ((childStyle & STYLE_ITALIC) && !(style & STYLE_ITALIC)) {
		g_string_append(out, "<i>");
		openedItalic = TRUE;
	}

	if ((childStyle & STYLE_UNDERLINE) && !(style & STYLE_UNDERLINE)) {
		g_string_append(out, "<u>");
		openedUnderline = TRUE;
	}

	if (node->content) {
		char* escaped = g_markup_escape_text(node->content, -1);
		g_string_append(out, escaped);
		g_free(escaped);
	}

	for (unsigned int i = 0; i < node->nChildren; i++)
		BuildMarkdownMarkup(node->children[i], out, childStyle);

	if (openedUnderline)
		g_string_append(out, "</u>");

	if (openedItalic)
		g_string_append(out, "</i>");

	if (openedBold)
		g_string_append(out, "</b>");
}

static void AppendImageFragment(GtkFlowBox* flow, MarkdownElement* node) {
	GtkWidget* image = NULL;

	if (node->content) {
		GError* err = NULL;
		GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(node->content, -1,
															  240, TRUE, &err);
		if (pixbuf) {
			image = gtk_image_new_from_pixbuf(pixbuf);
			g_object_unref(pixbuf);
		} else {
			g_warning("Failed to load image '%s': %s", node->content,
					  err ? err->message : "unknown error");
			if (err)
				g_error_free(err);
		}
	}

	if (!image) {
		image = gtk_label_new(node->alt ? node->alt : "[image]");
	}

	if (node->alt) {
		gtk_widget_set_tooltip_text(image, node->alt);
	}

	gtk_flow_box_insert(flow, image, -1);
}

typedef struct {
	char* data;
	size_t size;
} DynBuffer;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	size_t realsize = size * nmemb;
	DynBuffer* mem = userp;

	char* ptr = realloc(mem->data, mem->size + realsize + 1);
	if (!ptr) {
		return 0;
	}

	mem->data = ptr;
	memcpy(mem->data + mem->size, contents, realsize);
	mem->size += realsize;
	mem->data[mem->size] = '\0';

	return realsize;
}
int IsImageContentType(const char* ct) {
	if (!ct)
		return FALSE;
	return strncmp(ct, "image/gif", 9) == 0 ||
		   strncmp(ct, "image/png", 9) == 0 ||
		   strncmp(ct, "image/jpeg", 10) == 0;
}

GtkWidget* BuildMarkdownMessageWidget(const char* content) {
	MarkdownElement* tree = ParseMarkdownStr(content);

	GString* markup = g_string_new(NULL);
	BuildMarkdownMarkup(tree, markup, STYLE_NONE);

	GtkWidget* label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), markup->str);

	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_widget_set_halign(label, GTK_ALIGN_START);

	g_string_free(markup, TRUE);
	FreeMarkdownTree(tree);

	return label;
}

void PushUIMessage(GtkWidget* chatscroll, GtkWidget* chatarea, char* username,
				   char* displayname, char* content) {
	GtkWidget* msgrow = gtk_list_box_row_new();
	gtk_widget_set_hexpand(msgrow, TRUE);
	GtkWidget* msgbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(msgbox, TRUE);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(msgrow), msgbox);

	gtk_widget_set_halign(msgbox, GTK_ALIGN_START);
	gtk_widget_set_valign(msgbox, GTK_ALIGN_START);
	GtkWidget* msghbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_widget_set_halign(msgbox, GTK_ALIGN_START);
	gtk_widget_set_hexpand(msgbox, TRUE);
	GtkWidget* msgvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* usrtext = gtk_label_new(displayname);
	gtk_label_set_markup(GTK_LABEL(usrtext),
						 g_strdup_printf("<b>%s</b>", displayname));
	gtk_widget_set_halign(usrtext, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(msgvbox), usrtext);
	GtkWidget* msgtext = BuildMarkdownMessageWidget(content);
	GtkCssProvider* msgbubprovider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(
		msgbubprovider, "label { padding: 10px; border-radius: 10px; "
						"background-color: rgba(0, 0, 0, 0.2); }");

	GtkStyleContext* msgbubcontext = gtk_widget_get_style_context(msgtext);
	gtk_style_context_add_provider(msgbubcontext,
								   GTK_STYLE_PROVIDER(msgbubprovider),
								   GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_widget_set_halign(msgtext, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(msgvbox), msgtext);
	GtkWidget* userpfp = gtk_image_new_from_file(GetPfpPath(username));
	gtk_widget_set_valign(userpfp, GTK_ALIGN_START);
	gtk_image_set_pixel_size(GTK_IMAGE(userpfp), 42);
	gtk_box_append(GTK_BOX(msghbox), userpfp);
	gtk_box_append(GTK_BOX(msghbox), msgvbox);
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(msgrow), msghbox);
	gtk_list_box_append(GTK_LIST_BOX(chatarea), msgrow);

	GtkAdjustment* adj =
		gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chatscroll));
	gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
}
static gboolean gui_send_im(GtkEventControllerKey* controller, guint keyval,
							guint keycode, GdkModifierType state,
							gpointer user_data) {
	if (keyval == GDK_KEY_KP_Enter) {
		send_im_obj* dat = (send_im_obj*)user_data;
		GtkTextBuffer* buf =
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea));
		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter(buf, &start);
		gtk_text_buffer_get_end_iter(buf, &end);
		char* content = gtk_text_buffer_get_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea)), &start,
			&end, TRUE);
		YAMPSendIM(mainsock, dat->where, content);
		gtk_text_buffer_set_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(dat->EntryArea)), "", 0);
		return TRUE;
	}
	return FALSE;
}

void SpawnChatWindow(char* toWho) {
	GtkWidget* mainhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	if (GetChatWindow(toWho)) {
		return;
	}
	chat WhereParsed;
	if (!YAMPProcessWhere(toWho, curUsername, &WhereParsed)) {
		printf("malformed where, %s, returning\n", toWho);
		return;
	}
	if (WhereParsed.type == YAMP_GUILD) {
		printf("spawning a guild chat window at #%s\n",
			   WhereParsed.ChannelName);
	} else if (WhereParsed.type == YAMP_DM) {
		printf("spawning a DMs chat window for %s\n", WhereParsed.OtherGuy);
	}
	GtkWidget* chat_window = gtk_application_window_new(global_app);

	// vertical arragning box thing
	GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_box_append(GTK_BOX(mainhbox), vbox);

	// le chat area
	GtkWidget* scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
								   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll, TRUE);
	g_object_set_data(G_OBJECT(chat_window), "scroll", scroll);

	GtkWidget* chat_view = gtk_list_box_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), chat_view);
	g_object_set_data(G_OBJECT(chat_window), "chatview", chat_view);
	gtk_box_append(GTK_BOX(vbox), scroll);
	// input row
	GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_append(GTK_BOX(vbox), hbox);

	GtkWidget* upload_btn = gtk_button_new_with_label("+");
	gtk_box_append(GTK_BOX(hbox), upload_btn);
	GtkWidget* entry = gtk_text_view_new();
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_box_append(GTK_BOX(hbox), entry);

	gtk_window_set_child(GTK_WINDOW(chat_window), mainhbox);

	send_im_obj* dat = malloc(sizeof(send_im_obj));
	dat->EntryArea = entry;
	dat->ChatView = chat_view;
	dat->where = strdup(toWho);
	GtkEventController* key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(gui_send_im),
					 dat);

	if (WhereParsed.type == YAMP_DM) {
		char* window_title =
			malloc(6 + 3 + strlen(GetDisplayName(WhereParsed.OtherGuy)) + 1);
		sprintf(window_title, "Yampen - %s",
				GetDisplayName(WhereParsed.OtherGuy));
		gtk_window_set_title(GTK_WINDOW(chat_window), window_title);
		GtkWidget* detailsArea = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
		gtk_widget_set_hexpand(detailsArea, FALSE);
		gtk_widget_set_size_request(detailsArea, 340, -1);
		GtkCssProvider* detProvider = gtk_css_provider_new();
		gtk_css_provider_load_from_string(
			detProvider,
			"* { padding: 10px; background-color: rgba(0, 0, 0, 0.5); }");

		GtkStyleContext* detContext = gtk_widget_get_style_context(detailsArea);
		gtk_style_context_add_provider(detContext,
									   GTK_STYLE_PROVIDER(detProvider),
									   GTK_STYLE_PROVIDER_PRIORITY_USER);
		GtkWidget* pfp =
			gtk_image_new_from_file(GetPfpPath(WhereParsed.OtherGuy));
		gtk_image_set_pixel_size(GTK_IMAGE(pfp), 64);
		GtkWidget* dispnamelabel =
			gtk_label_new(GetDisplayName(WhereParsed.OtherGuy));
		gtk_box_append(GTK_BOX(detailsArea), pfp);
		gtk_box_append(GTK_BOX(detailsArea), dispnamelabel);
		if (GetProfileDescription(WhereParsed.OtherGuy)) {
			GtkWidget* desclabel =
				gtk_label_new(GetProfileDescription(WhereParsed.OtherGuy));
			gtk_label_set_wrap_mode(GTK_LABEL(desclabel), PANGO_WRAP_WORD);
			gtk_label_set_wrap(GTK_LABEL(desclabel), TRUE);
			gtk_label_set_max_width_chars(GTK_LABEL(desclabel), 30);
			gtk_widget_set_hexpand(desclabel, TRUE);
			GtkCssProvider* provider = gtk_css_provider_new();
			gtk_css_provider_load_from_string(
				provider, "label { padding: 10px; border-radius: 10px; "
						  "background-color: rgba(0, 0, 0, 0.5); }");

			GtkStyleContext* context = gtk_widget_get_style_context(desclabel);
			gtk_style_context_add_provider(context,
										   GTK_STYLE_PROVIDER(provider),
										   GTK_STYLE_PROVIDER_PRIORITY_USER);
			gtk_widget_set_halign(desclabel, GTK_ALIGN_START);
			gtk_box_append(GTK_BOX(detailsArea), desclabel);
		}
		gtk_box_append(GTK_BOX(mainhbox), detailsArea);
	} else {
		char* window_title = malloc(15 + strlen(WhereParsed.GuildName) +
									strlen(WhereParsed.ChannelName) + 1);
		sprintf(window_title, "Yampen - %s - #%s", WhereParsed.GuildName,
				WhereParsed.ChannelName);
		gtk_window_set_title(GTK_WINDOW(chat_window), window_title);
	}
	gtk_window_set_default_size(GTK_WINDOW(chat_window), 1200, 720);
	RegisterChatWindow(chat_window, dat->where);

	gtk_window_present(GTK_WINDOW(chat_window));
}
