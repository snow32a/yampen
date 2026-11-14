#include "gdk-pixbuf/gdk-pixbuf.h"
#include "gio/gmenu.h"
#include "glib-object.h"
#include "glib.h"
#include "globals.h"
#include "gtk/gtk.h"
#include "hashtables.h"
#include "protocol/yamp.h"
#include "message.h"
#include <curl/curl.h>
#include "chatwnd.h"
#include "imwnd.h"
#include <pthread.h>
typedef struct {
	char* where;
	GtkWidget* EntryArea;
	GtkWidget* ChatView;
} send_im_obj;
char* linkTo;

static GMenuModel* g_msg_menu_own;
static GMenuModel* g_msg_menu_other;
// it returns a heap str do not forget
char* GetUTF8Codepoints(char* input) {
	char* str = NULL;
	for (char* i = input; *i; i = g_utf8_next_char(i)) {
		gunichar c = g_utf8_get_char(i);
		if (str) {
			str = realloc(str, strlen(str) + 1 + 5 + 1);
			sprintf(str + strlen(str), "-%x", c);
		} else {
			str = malloc(5 + 1);
			sprintf(str, "%x", c);
		}
	}
	return str;
}
unsigned int GetDefaultPfp(const char* id) {
	unsigned rem = 0;

	for (int i = 0; i < 16; i++) {
		unsigned v;

		if (id[i] >= '0' && id[i] <= '9')
			v = id[i] - '0';
		else if (id[i] >= 'a' && id[i] <= 'z')
			v = id[i] - 'a' + 10;
		else
			return 0; // invalid ID

		rem = (rem * 36 + v) % 5;
	}

	return rem;
}

#define QUOTE_BAR "<span alpha=\"60%\">\xe2\x96\x8e </span>"

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
typedef struct {
	GtkWidget* root;
	GtkWidget* line;
} MdCtx;

void MdNewLine(MdCtx* ctx) {
	ctx->line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(ctx->line, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(ctx->root), ctx->line);
}
typedef struct {
	char* url;
	GtkWidget* pic;
	DynBuffer buf;
} AttachFetch;

gboolean AttachFetchDone(gpointer data) {
	AttachFetch* f = data;
	if (f->buf.data) {
		GBytes* bytes = g_bytes_new_with_free_func(f->buf.data, f->buf.size,
												   free, f->buf.data);
		GdkTexture* tex = gdk_texture_new_from_bytes(bytes, NULL);
		g_bytes_unref(bytes);
		if (tex) {
			int w = gdk_texture_get_width(tex);
			int h = gdk_texture_get_height(tex);
			double scale = MIN(1.0, MIN(400.0 / w, 300.0 / h));
			gtk_picture_set_paintable(GTK_PICTURE(f->pic), GDK_PAINTABLE(tex));
			gtk_widget_set_size_request(f->pic, w * scale, h * scale);
			g_object_unref(tex);
		}
	}
	g_object_unref(f->pic);
	free(f->url);
	free(f);
	return G_SOURCE_REMOVE;
}

gpointer AttachFetchThread(gpointer data) {
	AttachFetch* f = data;
	CURL* c = curl_easy_init();
	curl_easy_setopt(c, CURLOPT_URL, f->url);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &f->buf);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_perform(c);
	curl_easy_cleanup(c);
	g_idle_add(AttachFetchDone, f);
	return NULL;
}
void FreeAttachmentURLData(gpointer data, GClosure* closure) {
	(void)closure;
	free(data);
}


typedef struct {
	char* url;
	char* path;
	GtkWidget* btn;	   /* ref'd */
	GtkWindow* parent; /* weak-tracked, may become NULL */
	CURLcode rc;
	long http;
} DownloadJob;

static void FreeJob(DownloadJob* j) {
	if (j->parent)
		g_object_remove_weak_pointer(G_OBJECT(j->parent),
									 (gpointer*)&j->parent);
	g_object_unref(j->btn);
	g_free(j->url);
	g_free(j->path);
	g_free(j);
}

size_t WriteCb(void* p, size_t sz, size_t n, void* ud) {
	return fwrite(p, sz, n, (FILE*)ud);
}

char* FilenameFromUrl(const char* url) {
	char* tmp = g_strdup(url);
	char* cut = strpbrk(tmp, "?#");
	if (cut)
		*cut = '\0';

	const char* slash = strrchr(tmp, '/');
	const char* base = (slash && slash[1]) ? slash + 1 : "attachment";
	char* name = g_uri_unescape_string(base, NULL);
	g_free(tmp);
	return name ? name : g_strdup("attachment");
}

gboolean DownloadDone(gpointer data) {
	DownloadJob* j = data;

	gtk_widget_set_sensitive(j->btn, TRUE);

	if (j->rc != CURLE_OK || j->http >= 400) {
		remove(j->path);

		GtkAlertDialog* a = gtk_alert_dialog_new("Download failed");
		if (j->rc != CURLE_OK)
			gtk_alert_dialog_set_detail(a, curl_easy_strerror(j->rc));
		else {
			char* msg = g_strdup_printf("Server returned HTTP %i", j->http);
			gtk_alert_dialog_set_detail(a, msg);
			g_free(msg);
		}
		gtk_alert_dialog_show(a, j->parent);
		g_object_unref(a);
	}

	FreeJob(j);
	return G_SOURCE_REMOVE;
}

void* DownloadThread(void* data) {
	DownloadJob* j = data;

	FILE* f = fopen(j->path, "wb");
	if (!f) {
		j->rc = CURLE_WRITE_ERROR;
	} else {
		CURL* c = curl_easy_init();
		curl_easy_setopt(c, CURLOPT_URL, j->url);
		curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCb);
		curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
		curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

		j->rc = curl_easy_perform(c);
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &j->http);
		curl_easy_cleanup(c);
		fclose(f);
	}

	g_idle_add(DownloadDone, j);
	return NULL;
}

void OnSaveChosen(GObject* src, GAsyncResult* res, gpointer data) {
	DownloadJob* j = data;
	GError* err = NULL;

	GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
	if (!file) {
		g_clear_error(&err);
		FreeJob(j);
		return;
	}

	j->path = g_file_get_path(file);
	g_object_unref(file);

	if (!j->path) {
		GtkAlertDialog* a =
			gtk_alert_dialog_new("Please choose a local folder");
		gtk_alert_dialog_show(a, j->parent);
		g_object_unref(a);
		FreeJob(j);
		return;
	}

	gtk_widget_set_sensitive(j->btn, FALSE);
	pthread_t thread;
	pthread_create(&thread, NULL, DownloadThread, j);
}

gboolean OnAttachmentDownload(GtkButton* btn, char* url) {
	GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(btn));
	GtkWindow* parent = root ? GTK_WINDOW(root) : NULL;

	DownloadJob* j = malloc(sizeof(*j));
	*j = (DownloadJob){0};
	j->url = strdup(url);
	j->btn = GTK_WIDGET(g_object_ref(btn));
	j->parent = parent;
	if (parent)
		g_object_add_weak_pointer(G_OBJECT(parent), (gpointer*)&j->parent);

	char* name = FilenameFromUrl(url);

	GtkFileDialog* dlg = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dlg, "Save Attachment");
	gtk_file_dialog_set_initial_name(dlg, name);
	g_free(name);

	gtk_file_dialog_save(dlg, parent, NULL, OnSaveChosen, j);
	g_object_unref(dlg);
	return TRUE;
}
gboolean OnAttachmentFillerCB(GtkLinkButton* btn, gpointer user_data) {
	return TRUE;
}
void PushAttachments(GtkWidget* msgvbox, YampMessage* msg) {
	for (int i = 0; i < msg->nAttachments; i++) {
		char* url = ProcessAttachmentURL(msg->attachments[i]->url);
		if (!url)
			continue;

		if (IsImageContentType(msg->attachments[i]->mime)) {
			GtkWidget* pic = gtk_picture_new();
			gtk_picture_set_content_fit(GTK_PICTURE(pic),
										GTK_CONTENT_FIT_CONTAIN);
			gtk_widget_set_halign(pic, GTK_ALIGN_START);
			gtk_widget_set_size_request(pic, 256, 256);
			gtk_widget_add_css_class(pic, "attachment");
			gtk_widget_set_overflow(pic, GTK_OVERFLOW_HIDDEN);
			gtk_box_append(GTK_BOX(msgvbox), pic);

			AttachFetch* f = calloc(1, sizeof(AttachFetch));
			f->url = url;
			f->pic = g_object_ref(pic);
			g_thread_unref(g_thread_new("attach", AttachFetchThread, f));
		} else {
			char* name = strdup(msg->attachments[i]->filename)
							 ? strdup(msg->attachments[i]->filename)
							 : g_path_get_basename(msg->attachments[i]->url);
			char* q = strchr(name, '?');
			if (q)
				*q = '\0';
			GtkWidget* icon_wdg = gtk_image_new();
			GIcon* icon = g_content_type_get_icon(msg->attachments[i]->mime);

			gtk_image_set_from_gicon(GTK_IMAGE(icon_wdg), icon);
			gtk_image_set_pixel_size(GTK_IMAGE(icon_wdg), 48);

			g_object_unref(icon);

			GtkWidget* download_btn = gtk_button_new();
			GtkWidget* download_icon =
				gtk_image_new_from_icon_name("document-save-symbolic");
			gtk_widget_add_css_class(download_btn, "flat");
			gtk_widget_add_css_class(download_btn, "circular");
			gtk_button_set_child(GTK_BUTTON(download_btn), download_icon);
			gtk_widget_set_tooltip_text(download_btn, "Download attachment");

			GtkWidget* attachmentbox =
				gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
			gtk_widget_add_css_class(attachmentbox, "attachment-box");
			GtkWidget* btn = gtk_link_button_new_with_label(url, name);
			g_signal_connect_data(btn, "clicked",
								  G_CALLBACK(OnAttachmentDownload), strdup(url),
								  FreeAttachmentURLData, 0);
			g_signal_connect_data(download_btn, "clicked",
								  G_CALLBACK(OnAttachmentDownload), strdup(url),
								  FreeAttachmentURLData, 0);
			g_signal_connect(btn, "activate-link",
							 G_CALLBACK(OnAttachmentFillerCB), NULL);

			gtk_widget_set_halign(btn, GTK_ALIGN_FILL);
			gtk_box_append(GTK_BOX(attachmentbox), icon_wdg);
			gtk_box_append(GTK_BOX(attachmentbox), btn);
			gtk_box_append(GTK_BOX(attachmentbox), download_btn);
			gtk_box_append(GTK_BOX(msgvbox), attachmentbox);
			g_free(name);
		}
	}
}
void PushChannelBanner(GtkWidget* chatarea, YampChannel* channel) {
	char* title = NULL;
	char* body = NULL;

	if (channel->type >= YAMP_SPACE_TEXT) {
		title = g_strdup_printf("Welcome to #%s", channel->name);
		body = g_strdup_printf("This is the start of #%s.", channel->name);
	} else if (channel->type == YAMP_GC) {
		if (channel->name) {
			title = g_strdup_printf("Welcome to %s!", channel->name);
		} else {
			title = g_strdup("Welcome to the group!");
		}
		body = g_strdup("Have a nice chat w/ your friends!");
	} else if (channel->type == YAMP_DM) {
		title = g_strdup("Welcome!");
		char* other_guy;
		if (strcmp(curUserID, channel->people[0]->id) == 0) {
			other_guy = channel->people[1]->displayname
							? channel->people[1]->displayname
							: channel->people[1]->username;
		} else {
			other_guy = channel->people[0]->displayname
							? channel->people[0]->displayname
							: channel->people[0]->username;
		}
		body = g_strdup_printf(
			"This is the start of your conversation with %s!", channel->name);
	} else {
		return;
	}

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_halign(box, GTK_ALIGN_FILL);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);

	GtkWidget* header = gtk_label_new(title);
	gtk_widget_add_css_class(header, "h1");
	gtk_widget_set_halign(header, GTK_ALIGN_START);
	gtk_label_set_xalign(GTK_LABEL(header), 0.0);
	gtk_label_set_wrap(GTK_LABEL(header), TRUE);

	GtkWidget* text = gtk_label_new(body);
	gtk_widget_add_css_class(text, "banner-text");
	gtk_widget_set_halign(text, GTK_ALIGN_START);
	gtk_label_set_xalign(GTK_LABEL(text), 0.0);
	gtk_label_set_wrap(GTK_LABEL(text), TRUE);

	gtk_box_append(GTK_BOX(box), header);
	gtk_box_append(GTK_BOX(box), text);

	GtkWidget* row = gtk_list_box_row_new();
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
	gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
	gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);

	gtk_list_box_append(GTK_LIST_BOX(chatarea), row);

	g_free(title);
	g_free(body);
}
void FillContentBox(MdCtx* ctx, MarkdownElement* tree, int inheritedStyle) {
	for (int i = 0; i < tree->nChildren; i++) {
		MarkdownElement* child = tree->children[i];
		if (child->type == MARKDOWN_EMOJI) {
			char* emoji_codepoint = GetUTF8Codepoints(child->content);
			char filename_str[44 + strlen(emoji_codepoint) + 1];
			sprintf(filename_str,
					"/org/yampen/assets/twemoji/assets/72x72/%s.png",
					emoji_codepoint);
			GtkWidget* img = gtk_image_new_from_resource(filename_str);
			gtk_image_set_pixel_size(GTK_IMAGE(img),
									 tree->nChildren - 1 ? 22 : 48);
			free(emoji_codepoint);
			gtk_box_append(GTK_BOX(ctx->line), img);
			continue;
		}
		if (child->type == MARKDOWN_CODEBLOCK) {
			MdNewLine(ctx);
			GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
			GtkWidget* lbl = gtk_label_new(child->content);
			gtk_box_append(GTK_BOX(box), lbl);
			gtk_widget_add_css_class(lbl, "monospace");
			gtk_widget_add_css_class(lbl, "inline-code");
			GtkWidget* srcinfo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_append(GTK_BOX(srcinfo), gtk_label_new("Source Block"));
			gtk_box_append(GTK_BOX(ctx->line), box);
			MdNewLine(ctx);
			continue;
		}
		GtkWidget* lbl = gtk_label_new(child->content);
		int ChildStyle = inheritedStyle | child->type;
		int block = 0;
		if (ChildStyle & MARKDOWN_BOLD) {
			gtk_widget_add_css_class(lbl, "bold");
		}
		if (ChildStyle & MARKDOWN_ITALIC) {
			gtk_widget_add_css_class(lbl, "italic");
		}
		if (ChildStyle & MARKDOWN_H1) {
			gtk_widget_add_css_class(lbl, "h1");
			block = 1;
		}
		if (ChildStyle & MARKDOWN_H2) {
			gtk_widget_add_css_class(lbl, "h2");
			block = 1;
		}
		if (ChildStyle & MARKDOWN_H3) {
			gtk_widget_add_css_class(lbl, "h3");
			block = 1;
		}
		if (ChildStyle & MARKDOWN_UNDERLINE) {
			gtk_widget_add_css_class(lbl, "underline");
		}
		if (ChildStyle & MARKDOWN_STRIKE) {
			gtk_widget_add_css_class(lbl, "strike");
		}
		if (ChildStyle & MARKDOWN_SPOILER) {
			gtk_widget_add_css_class(lbl,
									 "spoiler"); // TODO add gestures here so it
												 // shows when its clicked
		}
		if (ChildStyle & MARKDOWN_CODE) {
			gtk_widget_add_css_class(lbl, "monospace");
			gtk_widget_add_css_class(lbl, "inline-code");
		}
		gtk_box_append(GTK_BOX(ctx->line), lbl);
		if (child->nChildren) {
			FillContentBox(ctx, child, ChildStyle);
		}
		if (block) {
			MdNewLine(ctx);
		}
	}
}
GtkWidget* BuildMarkdownMessageWidget(const char* content) {
	MarkdownElement* tree = ParseMarkdownStr(content);

	MdCtx ctx;
	ctx.root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_halign(ctx.root, GTK_ALIGN_START);
	MdNewLine(&ctx);
	FillContentBox(&ctx, tree, MARKDOWN_TEXT);
	FreeMarkdownTree(tree);

	return ctx.root;
}

void ChatAreaSizeChanged(GtkWidget* widget, GParamSpec* pspec, gpointer data) {
	GtkScrolledWindow* scroll = GTK_SCROLLED_WINDOW(data);

	GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(scroll);

	double bottom =
		gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);

	gtk_adjustment_set_value(adj, MAX(0.0, bottom));
}
void HandleMsgContextMenu(GtkGestureClick* gesture, int n_press, double x,
						  double y, gpointer user_data) {
	GtkWidget* row =
		gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GtkPopover* pop = g_object_get_data(G_OBJECT(row), "ctxmenu");
	if (!pop) {
		gboolean own = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "own"));
		pop = GTK_POPOVER(gtk_popover_menu_new_from_model(
			own ? g_msg_menu_own : g_msg_menu_other));
		gtk_popover_set_has_arrow(pop, FALSE);
		gtk_widget_set_parent(GTK_WIDGET(pop), row);
		g_object_set_data(G_OBJECT(row), "ctxmenu", pop);
		g_signal_connect_swapped(row, "destroy",
								 G_CALLBACK(gtk_widget_unparent), pop);
	}

	GdkRectangle rect = {.x = (int)x, .y = (int)y, .width = 1, .height = 1};

	gtk_popover_set_pointing_to(pop, &rect);
	gtk_popover_set_position(pop, GTK_POS_BOTTOM);
	gtk_widget_set_halign(GTK_WIDGET(pop), GTK_ALIGN_START);
	gtk_popover_popup(pop);
}

void ClearReply(void) {
	FreeMessageObject(ReplyingToMsg);
	isReplying = 0;
	if (replyindicator)
		gtk_widget_set_visible(replyindicator, 0);
}

void ReplyCallback(GSimpleAction* action, GVariant* parameter,
				   gpointer user_data) {
	GObject* row = G_OBJECT(user_data);
	const char* msgid = g_object_get_data(row, "msgid");
	const char* userid = g_object_get_data(row, "userid");
	const char* dispname = g_object_get_data(row, "dispname");
	const char* content = g_object_get_data(row, "content");

	ClearReply();

	ReplyingToMsg = malloc(sizeof(*ReplyingToMsg));
	*ReplyingToMsg = (YampMessage){0};
	ReplyingToMsg->author = malloc(sizeof(YampUser));
	strncpy(ReplyingToMsg->id, msgid, sizeof(ReplyingToMsg->id) - 1);
	strncpy(ReplyingToMsg->author->id, userid, sizeof(ReplyingToMsg->author->id) - 1);
	ReplyingToMsg->author->displayname = strdup(dispname ? dispname : "Unknown");
	ReplyingToMsg->content = strdup(content ? content : "");
	isReplying = 1;

	char* lbltext = g_markup_printf_escaped("Reply to <b>%s</b>",
											ReplyingToMsg->author->displayname);
	gtk_label_set_markup(GTK_LABEL(replyindicator), lbltext);
	g_free(lbltext);
	gtk_widget_set_visible(replyindicator, 1);
}
void DeleteCallback(GSimpleAction* action, GVariant* parameter,
					gpointer user_data) {
	YAMPDeleteMessage(mainsock, g_object_get_data(user_data, "msgid"), NULL);
}
void JumpToReplyTarget(GtkGestureClick* g, int n_press, double x, double y,
					   gpointer user_data) {
	GtkWidget* banner = GTK_WIDGET(user_data);
	const char* target = g_object_get_data(G_OBJECT(banner), "target-id");
	GtkWidget* chatarea = g_object_get_data(G_OBJECT(banner), "chatarea");
	GtkWidget* chatscroll = g_object_get_data(G_OBJECT(banner), "chatscroll");
	if (!target || !chatarea || !chatscroll)
		return;

	for (GtkWidget* row = gtk_widget_get_first_child(chatarea); row;
		 row = gtk_widget_get_next_sibling(row)) {
		const char* id = g_object_get_data(G_OBJECT(row), "msgid");
		if (!id || strcmp(id, target) != 0)
			continue;

		graphene_rect_t b;
		if (gtk_widget_compute_bounds(row, chatarea, &b)) {
			GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(
				GTK_SCROLLED_WINDOW(chatscroll));
			double page = gtk_adjustment_get_page_size(vadj);
			gtk_adjustment_set_value(vadj,
									 b.origin.y - (page - b.size.height) / 2);
		}
		return;
	}
}
void DrawReplyCurve(GtkDrawingArea* area, cairo_t* cr, int width, int height,
					gpointer data) {
	cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.7);
	cairo_set_line_width(cr, 2.0);

	cairo_move_to(cr, width - 5, height / 2);

	cairo_curve_to(cr, width - 5, height / 2, width / 2, height / 2, width / 2,
				   height - 5);

	cairo_stroke(cr);
}
void DeleteUIMessage(GtkWidget* chatscroll, GtkWidget* chatarea, char* id) {
	for (GtkWidget* msg = gtk_widget_get_first_child(chatarea); msg;
		 msg = gtk_widget_get_next_sibling(msg)) {
		const char* mid = g_object_get_data(G_OBJECT(msg), "msgid");
		if (mid && strcmp(mid, id) == 0) {
			gtk_list_box_remove(GTK_LIST_BOX(chatarea), msg);
			return;
		}
	}
}
void InitMsgMenus(void) {
	if (g_msg_menu_own)
		return;
	GMenu* own = g_menu_new();
	g_menu_append(own, "Reply", "row.reply");
	g_menu_append(own, "Delete", "row.delete");
	GMenu* other = g_menu_new();
	g_menu_append(other, "Reply", "row.reply");
	g_msg_menu_own = G_MENU_MODEL(own);
	g_msg_menu_other = G_MENU_MODEL(other);
}
GtkWidget* PushUIMessage(GtkWidget* chatscroll, GtkWidget* chatarea,
						 YampMessage* msg, int silhouette) {
	GtkGesture* gesture = gtk_gesture_click_new();

	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
	g_signal_connect(gesture, "pressed", G_CALLBACK(HandleMsgContextMenu),
					 NULL);
	GtkWidget* lastmsgrow = gtk_widget_get_last_child(chatarea);
	GtkWidget* msgrow = NULL;
	GtkWidget* msgvbox = NULL;
	GtkWidget* outer = NULL;
	GtkWidget* msgbox = NULL;

	const char* last_userid = NULL;

	if (lastmsgrow)
		last_userid = g_object_get_data(G_OBJECT(lastmsgrow), "userid");

	if (last_userid && strcmp(msg->author->id, last_userid) == 0 &&
		!msg->reply) {
		msgrow = gtk_list_box_row_new();
		gtk_widget_set_hexpand(msgrow, TRUE);

		g_object_set_data_full(G_OBJECT(msgrow), "userid",
							   g_strdup(msg->author->id), free);
		g_object_set_data_full(G_OBJECT(msgrow), "msgid", g_strdup(msg->id),
							   free);
		g_object_set_data_full(G_OBJECT(msgrow), "dispname",
							   g_strdup(msg->author->displayname
											? msg->author->displayname
											: msg->author->username),
							   free);

		outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		msgbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		gtk_widget_set_hexpand(msgbox, TRUE);
		gtk_widget_set_halign(msgbox, GTK_ALIGN_START);
		gtk_widget_set_valign(msgbox, GTK_ALIGN_START);

		GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_size_request(spacer, 42, -1);
		gtk_widget_set_hexpand(spacer, FALSE);
		gtk_widget_set_valign(spacer, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(msgbox), spacer);

		msgvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_halign(msgvbox, GTK_ALIGN_START);
		gtk_widget_set_hexpand(msgvbox, TRUE);
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(msgrow), outer);

		gtk_box_append(GTK_BOX(msgbox), msgvbox);
	} else {
		msgrow = gtk_list_box_row_new();
		gtk_widget_set_hexpand(msgrow, TRUE);

		g_object_set_data_full(G_OBJECT(msgrow), "userid",
							   g_strdup(msg->author->id), free);
		g_object_set_data_full(G_OBJECT(msgrow), "msgid", g_strdup(msg->id),
							   free);
		g_object_set_data_full(G_OBJECT(msgrow), "dispname",
							   g_strdup(msg->author->displayname
											? msg->author->displayname
											: msg->author->username),
							   free);

		outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		msgbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
		gtk_widget_set_hexpand(msgbox, TRUE);
		gtk_widget_set_halign(msgbox, GTK_ALIGN_START);
		gtk_widget_set_valign(msgbox, GTK_ALIGN_START);

		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(msgrow), outer);

		GtkWidget* userpfp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_add_css_class(userpfp, "avatar");
		gtk_widget_set_overflow(userpfp, GTK_OVERFLOW_HIDDEN);
		gtk_widget_set_valign(userpfp, GTK_ALIGN_START);

		GtkWidget* userpfpinner;

		const char* cached = GetPfpPath(msg->author->id);
		if (cached && g_file_test(cached, G_FILE_TEST_IS_REGULAR)) {
			userpfpinner = gtk_image_new_from_file(cached);
		} else {
			char defaultpfppath[64];
			snprintf(defaultpfppath, sizeof(defaultpfppath),
					 "/org/yampen/assets/pfps/default%i.png",
					 GetDefaultPfp(msg->author->id));
			userpfpinner = gtk_image_new_from_resource(defaultpfppath);
		}

		gtk_image_set_pixel_size(GTK_IMAGE(userpfpinner), 42);
		gtk_box_append(GTK_BOX(userpfp), userpfpinner);
		gtk_box_append(GTK_BOX(msgbox), userpfp);

		msgvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_halign(msgvbox, GTK_ALIGN_START);
		gtk_widget_set_hexpand(msgvbox, TRUE);

		GtkWidget* usrtext = gtk_label_new(NULL);

		char* markup = g_markup_printf_escaped(
			"<b>%s</b>", msg->author->displayname ? msg->author->displayname
												  : msg->author->username);
		gtk_label_set_markup(GTK_LABEL(usrtext), markup);
		g_free(markup);

		gtk_widget_set_halign(usrtext, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(msgvbox), usrtext);

		gtk_box_append(GTK_BOX(msgbox), msgvbox);
	}
	// generic stuff applying to both branches
	g_object_set_data(G_OBJECT(msgrow), "own",
					  GINT_TO_POINTER(strcmp(curUserID, msg->author->id) == 0));
	if (msg->reply) {
		GtkWidget* replyrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

		GtkWidget* curve = gtk_drawing_area_new();

		gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(curve), 42);
		gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(curve), 20);

		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(curve), DrawReplyCurve,
									   NULL, NULL);
		gtk_box_append(GTK_BOX(replyrow), curve);

		GtkWidget* replymsg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
		gtk_widget_set_opacity(replymsg, 0.8f);

		GtkWidget* replyusername = gtk_label_new(
			msg->reply->author->displayname ? msg->reply->author->displayname
											: msg->reply->author->username);
		gtk_label_set_xalign(GTK_LABEL(replyusername), 0.0f);
		gtk_widget_add_css_class(replyusername, "bold");

		GtkWidget* replycontent = gtk_label_new(msg->reply->content);
		gtk_label_set_xalign(GTK_LABEL(replycontent), 0.0f);

		gtk_widget_set_halign(replymsg, GTK_ALIGN_START);

		gtk_box_append(GTK_BOX(replymsg), replyusername);
		gtk_box_append(GTK_BOX(replymsg), replycontent);
		gtk_box_append(GTK_BOX(replyrow), replymsg);

		gtk_box_append(GTK_BOX(outer), replyrow);
	}
	gtk_box_append(GTK_BOX(outer), msgbox);

	gtk_list_box_append(GTK_LIST_BOX(chatarea), msgrow);


	// finish the rest of that context menu stuff here below, that needs msgrow
	// etc

	GSimpleActionGroup* grp = g_simple_action_group_new();
	GSimpleAction* a = g_simple_action_new("reply", NULL);
	g_signal_connect(a, "activate", G_CALLBACK(ReplyCallback), msgrow);
	g_action_map_add_action(G_ACTION_MAP(grp), G_ACTION(a));
	g_object_unref(a);
	if (strcmp(curUserID, msg->author->id) == 0) {
		GSimpleAction* a = g_simple_action_new("delete", NULL);
		g_signal_connect(a, "activate", G_CALLBACK(DeleteCallback), msgrow);
		g_action_map_add_action(G_ACTION_MAP(grp), G_ACTION(a));
		g_object_unref(a);
	}
	gtk_widget_insert_action_group(msgrow, "row", G_ACTION_GROUP(grp));
	g_object_unref(grp);

	gtk_widget_add_controller(msgrow, GTK_EVENT_CONTROLLER(gesture));

	GtkWidget* msgtext = BuildMarkdownMessageWidget(msg->content);
	if (silhouette) {
		gtk_widget_set_opacity(msgtext, 0.5f);
	}

	if (msg->content && strlen(msg->content)) {
		GtkCssProvider* msgbubprovider = gtk_css_provider_new();
		gtk_css_provider_load_from_string(
			msgbubprovider, "box { padding: 10px; border-radius: 10px; "
							"background-color: rgba(0, 0, 0, 0.2); }");

		gtk_style_context_add_provider(gtk_widget_get_style_context(msgtext),
									   GTK_STYLE_PROVIDER(msgbubprovider),
									   GTK_STYLE_PROVIDER_PRIORITY_USER);

		g_object_unref(msgbubprovider);

		gtk_widget_set_halign(msgtext, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(msgvbox), msgtext);
	}
	PushAttachments(msgvbox, msg);

	GtkAdjustment* vadj =
		gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chatscroll));

	gtk_adjustment_set_value(vadj, gtk_adjustment_get_upper(vadj) -
									   gtk_adjustment_get_page_size(vadj));
	return msgrow;
}