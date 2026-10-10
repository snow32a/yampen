#include "gdk-pixbuf/gdk-pixbuf.h"
#include "glib-object.h"
#include "globals.h"
#include "gtk/gtk.h"
#include "hashtables.h"
#include "protocol/yamp.h"
#include "message.h"
#include <curl/curl.h>
#include "hmap/hashmap.h"
#include "chatwnd.h"
typedef struct {
	char* where;
	GtkWidget* EntryArea;
	GtkWidget* ChatView;
} send_im_obj;
char* linkTo;
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
#define CODE_STYLE "background=\"#2b2b2b\" foreground=\"#e0e0e0\""
#define SPOILER_STYLE "background=\"#4a4a4a\" foreground=\"#4a4a4a\""

static void BuildMarkdownMarkup(MarkdownElement* node, GString* out);

static void AppendEscaped(GString* out, const char* s) {
	if (!s)
		return;
	char* valid = g_utf8_make_valid(s, -1);
	char* esc = g_markup_escape_text(valid, -1);
	g_string_append(out, esc);
	g_free(esc);
	g_free(valid);
}

/* the parser eats the line break after these, so we put it back */
static gboolean IsLineBlock(int type) {
	return type == MARKDOWN_H1 || type == MARKDOWN_H1 + 1 ||
		   type == MARKDOWN_H1 + 2 || type == MARKDOWN_SUBTEXT ||
		   type == MARKDOWN_QUOTE;
}

static void BuildChildren(MarkdownElement* node, GString* out) {
	for (int i = 0; i < node->nChildren; i++) {
		BuildMarkdownMarkup(node->children[i], out);
		if (i + 1 < node->nChildren && IsLineBlock(node->children[i]->type))
			g_string_append_c(out, '\n');
	}
}

static void Wrap(MarkdownElement* node, GString* out, const char* open,
				 const char* close) {
	g_string_append(out, open);
	BuildChildren(node, out);
	g_string_append(out, close);
}

static void BuildMarkdownMarkup(MarkdownElement* node, GString* out) {
	if (!node)
		return;

	switch (node->type) {
	case MARKDOWN_BOLD:
		Wrap(node, out, "<b>", "</b>");
		break;
	case MARKDOWN_ITALIC:
		Wrap(node, out, "<i>", "</i>");
		break;
	case MARKDOWN_UNDERLINE:
		Wrap(node, out, "<u>", "</u>");
		break;
	case MARKDOWN_STRIKE:
		Wrap(node, out, "<s>", "</s>");
		break;
	case MARKDOWN_SPOILER:
		Wrap(node, out, "<span " SPOILER_STYLE ">", "</span>");
		break;

	case MARKDOWN_LINK: {
		char* url = g_markup_escape_text(node->content, -1);
		g_string_append_printf(out, "<a href=\"%s\">", url);
		g_free(url);
		BuildChildren(node, out);
		g_string_append(out, "</a>");
		break;
	}

	case MARKDOWN_CODE:
		g_string_append(out, "<tt><span " CODE_STYLE ">");
		AppendEscaped(out, node->content);
		g_string_append(out, "</span></tt>");
		break;

	case MARKDOWN_CODEBLOCK:
		if (out->len && out->str[out->len - 1] != '\n')
			g_string_append_c(out, '\n');
		g_string_append(out, "<tt><span " CODE_STYLE ">");
		AppendEscaped(out, node->content);
		g_string_append(out, "</span></tt>");
		break;

	case MARKDOWN_EMOJI:
		g_string_append(out, "<span size=\"x-large\">");
		AppendEscaped(out, node->content);
		g_string_append(out, "</span>");
		break;

	case MARKDOWN_H1:
		Wrap(node, out, "<span size=\"xx-large\" weight=\"bold\">", "</span>");
		break;
	case MARKDOWN_H1 + 1: /* H2 */
		Wrap(node, out, "<span size=\"x-large\" weight=\"bold\">", "</span>");
		break;
	case MARKDOWN_H1 + 2: /* H3 */
		Wrap(node, out, "<span size=\"large\" weight=\"bold\">", "</span>");
		break;
	case MARKDOWN_SUBTEXT:
		Wrap(node, out, "<span size=\"small\" alpha=\"60%\">", "</span>");
		break;

	case MARKDOWN_QUOTE: {
		GString* inner = g_string_new(NULL);
		BuildChildren(node, inner);
		g_string_append(out, QUOTE_BAR);
		for (const char* p = inner->str; *p; p++) {
			g_string_append_c(out, *p);
			if (*p == '\n') /* bar on every quoted line */
				g_string_append(out, QUOTE_BAR);
		}
		g_string_free(inner, TRUE);
		break;
	}

	case MARKDOWN_TEXT:
	default:
		AppendEscaped(out, node->content);
		BuildChildren(node, out);
		break;
	}
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
	BuildMarkdownMarkup(tree, markup);

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

void ChatAreaSizeChanged(GtkWidget* widget, GParamSpec* pspec, gpointer data) {
	GtkScrolledWindow* scroll = GTK_SCROLLED_WINDOW(data);

	GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(scroll);

	double bottom =
		gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);

	gtk_adjustment_set_value(adj, MAX(0.0, bottom));
}
void PushUIMessage(GtkWidget* chatscroll, GtkWidget* chatarea, char* userid,
                   char* displayname, char* pfppath, char* content) {
    GtkWidget* lastmsgrow = gtk_widget_get_last_child(chatarea);
    GtkWidget* msgrow = NULL;
    GtkWidget* msgvbox = NULL;

    const char* last_userid = NULL;

    if (lastmsgrow)
        last_userid = g_object_get_data(G_OBJECT(lastmsgrow), "userid");

    if (last_userid && strcmp(userid, last_userid) == 0) {
        msgrow = lastmsgrow;

        GtkWidget* msgbox =
            gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(msgrow));

        msgvbox = gtk_widget_get_last_child(msgbox);
    } else {
        msgrow = gtk_list_box_row_new();
        gtk_widget_set_hexpand(msgrow, TRUE);

        g_object_set_data_full(G_OBJECT(msgrow), "userid",
                               g_strdup(userid), free);

        GtkWidget* msgbox =
            gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_widget_set_hexpand(msgbox, TRUE);
        gtk_widget_set_halign(msgbox, GTK_ALIGN_START);
        gtk_widget_set_valign(msgbox, GTK_ALIGN_START);

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(msgrow), msgbox);

        GtkWidget* userpfp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(userpfp, "avatar");
        gtk_widget_set_overflow(userpfp, GTK_OVERFLOW_HIDDEN);
        gtk_widget_set_valign(userpfp, GTK_ALIGN_START);

        GtkWidget* userpfpinner;

        if (!pfppath) {
            char defaultpfppath[64];

            snprintf(defaultpfppath, sizeof(defaultpfppath),
                     "/org/yampen/assets/pfps/default%i.png",
                     GetDefaultPfp(userid));

            userpfpinner =
                gtk_image_new_from_resource(defaultpfppath);
        } else {
            userpfpinner = gtk_image_new_from_file(pfppath);
        }

        gtk_image_set_pixel_size(GTK_IMAGE(userpfpinner), 42);
        gtk_box_append(GTK_BOX(userpfp), userpfpinner);
        gtk_box_append(GTK_BOX(msgbox), userpfp);

        msgvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_halign(msgvbox, GTK_ALIGN_START);
        gtk_widget_set_hexpand(msgvbox, TRUE);

        GtkWidget* usrtext = gtk_label_new(NULL);

        char* markup = g_markup_printf_escaped("<b>%s</b>", displayname);
        gtk_label_set_markup(GTK_LABEL(usrtext), markup);
        g_free(markup);

        gtk_widget_set_halign(usrtext, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(msgvbox), usrtext);

        gtk_box_append(GTK_BOX(msgbox), msgvbox);

        gtk_list_box_append(GTK_LIST_BOX(chatarea), msgrow);
    }

    /* Actual message */
    GtkWidget* msgtext = BuildMarkdownMessageWidget(content);

    GtkCssProvider* msgbubprovider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        msgbubprovider,
        "label { padding: 10px; border-radius: 10px; "
        "background-color: rgba(0, 0, 0, 0.2); }");

    gtk_style_context_add_provider(
        gtk_widget_get_style_context(msgtext),
        GTK_STYLE_PROVIDER(msgbubprovider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);

    g_object_unref(msgbubprovider);

    gtk_widget_set_halign(msgtext, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(msgvbox), msgtext);

    GtkAdjustment* vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(chatscroll));

    gtk_adjustment_set_value(
        vadj,
        gtk_adjustment_get_upper(vadj) -
        gtk_adjustment_get_page_size(vadj));
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
