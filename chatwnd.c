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
#include "hmap/hashmap.h"
#include "chatwnd.h"
#include "imwnd.h"
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

static void MdNewLine(MdCtx* ctx) {
	ctx->line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(ctx->line, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(ctx->root), ctx->line);
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
void ReplyCallback(GSimpleAction* action, GVariant* parameter,
				   gpointer user_data) {
	gtk_widget_set_visible(replyindicator, 1);
	char* lbltext =
		malloc(17 + strlen(g_object_get_data(user_data, "dispname")));
	sprintf(lbltext, "Reply to <b>%s</b>",
			g_object_get_data(user_data, "dispname"));
	gtk_label_set_markup(GTK_LABEL(replyindicator), lbltext);
	ReplyingToMsg = g_object_get_data(user_data, "msgid");
	isReplying = 1;
}
void DeleteCallback(GSimpleAction* action, GVariant* parameter,
					gpointer user_data) {
	YAMPDeleteMessage(mainsock, g_object_get_data(user_data, "msgid"));
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
		if (strcmp(g_object_get_data(G_OBJECT(msg), "msgid"), id) == 0) {
			gtk_list_box_remove(GTK_LIST_BOX(chatarea),msg);
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

	if (last_userid && strcmp(msg->author.id, last_userid) == 0 &&
		!msg->reply) {
		msgrow = gtk_list_box_row_new();
		gtk_widget_set_hexpand(msgrow, TRUE);

		g_object_set_data_full(G_OBJECT(msgrow), "userid",
							   g_strdup(msg->author.id), free);
		g_object_set_data_full(G_OBJECT(msgrow), "msgid", g_strdup(msg->id),
							   free);
		g_object_set_data_full(G_OBJECT(msgrow), "dispname",
							   g_strdup(msg->author.displayname
											? msg->author.displayname
											: msg->author.username),
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
							   g_strdup(msg->author.id), free);
		g_object_set_data_full(G_OBJECT(msgrow), "msgid", g_strdup(msg->id),
							   free);
		g_object_set_data_full(G_OBJECT(msgrow), "dispname",
							   g_strdup(msg->author.displayname
											? msg->author.displayname
											: msg->author.username),
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

		if (!GetPfpPath(msg->author.id)) {
			char defaultpfppath[64];

			snprintf(defaultpfppath, sizeof(defaultpfppath),
					 "/org/yampen/assets/pfps/default%i.png",
					 GetDefaultPfp(msg->author.id));

			userpfpinner = gtk_image_new_from_resource(defaultpfppath);
		} else {
			userpfpinner = gtk_image_new_from_file(GetPfpPath(msg->author.id));
		}

		gtk_image_set_pixel_size(GTK_IMAGE(userpfpinner), 42);
		gtk_box_append(GTK_BOX(userpfp), userpfpinner);
		gtk_box_append(GTK_BOX(msgbox), userpfp);

		msgvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_halign(msgvbox, GTK_ALIGN_START);
		gtk_widget_set_hexpand(msgvbox, TRUE);

		GtkWidget* usrtext = gtk_label_new(NULL);

		char* markup = g_markup_printf_escaped(
			"<b>%s</b>", msg->author.displayname ? msg->author.displayname
												 : msg->author.username);
		gtk_label_set_markup(GTK_LABEL(usrtext), markup);
		g_free(markup);

		gtk_widget_set_halign(usrtext, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(msgvbox), usrtext);

		gtk_box_append(GTK_BOX(msgbox), msgvbox);
	}
	// generic stuff applying to both branches
	g_object_set_data(G_OBJECT(msgrow), "own",
				  GINT_TO_POINTER(strcmp(curUserID, msg->author.id) == 0));
	if (msg->reply) {
		GtkWidget* replyrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

		GtkWidget* curve = gtk_drawing_area_new();

		gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(curve), 42);
		gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(curve), 20);

		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(curve), DrawReplyCurve,
									   NULL, NULL);
		gtk_box_append(GTK_BOX(replyrow), curve);


		GtkWidget* replyborder = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_size_request(replyborder, 4, -1);
		gtk_widget_add_css_class(replyborder, "reply_border");
		gtk_box_append(GTK_BOX(replyrow), replyborder);

		GtkWidget* replymsg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
		gtk_widget_add_css_class(replymsg, "reply_body");

		GtkWidget* replyusername = gtk_label_new(
			msg->reply->author.displayname ? msg->reply->author.displayname
										   : msg->reply->author.username);
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
	if (strcmp(curUserID, msg->author.id) == 0) {
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

	GtkAdjustment* vadj =
		gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chatscroll));

	gtk_adjustment_set_value(vadj, gtk_adjustment_get_upper(vadj) -
									   gtk_adjustment_get_page_size(vadj));
	return msgrow;
}