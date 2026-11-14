#include "chatinput.h"
#include "protocol/yamp.h"
#include "emojipopover.h"
#include "globals.h"
#include "imwnd.h"
#include "hashtables.h"
#include "chatwnd.h"
int LastTypedAt = 0;
typedef struct {
	YampMessage* msg;
	YampMessage* reply;
	int nonce;
	int pending;
	char channel[34];
} PendingSend;

typedef struct {
	PendingSend* ps;
	YampAttachment* att;
} UploadCtx;

void FinishSend(PendingSend* ps) {
	YAMPSendMessage(mainsock, ps->channel, ps->msg, ps->nonce, NULL);
	for (int i = 0; i < ps->msg->nAttachments; i++) {
		free((gchar*)ps->msg->attachments[i]->url);
		free((gchar*)ps->msg->attachments[i]->mime);
		free((gchar*)ps->msg->attachments[i]->filename);
	}
	free(ps->msg->attachments);
	free((gchar*)ps->msg->content);
	free(ps);
	curAttachements = NULL;
}
typedef struct {
	UploadCtx* uc;
	char* url;
} UploadDone;

static gboolean UploadDoneIdle(gpointer p) {
	UploadDone* d = p;
	UploadCtx* uc = d->uc;
	PendingSend* ps = uc->ps;

	if (d->url)
		uc->att->url = d->url;

	free(uc);
	g_free(d);

	if (--ps->pending == 0)
		FinishSend(ps);
	return G_SOURCE_REMOVE;
}

void YAMPUploadAttachmentCB(char* url, int success, void* ud) {
	UploadDone* d = g_new0(UploadDone, 1);
	d->uc = ud;
	d->url = (success && url) ? strdup(url) : NULL;
	g_idle_add(UploadDoneIdle, d);
}
int EntryKeyHandler(GtkEventControllerKey* controller, guint keyval,
					guint keycode, GdkModifierType state, gpointer user_data) {
	if (time(NULL) - LastTypedAt > 5 && currentChannel) {
		YAMPTyping(mainsock, currentChannel->id, NULL);
	}
	EKeyPayload* dat = user_data;
	GtkTextView* tv = GTK_TEXT_VIEW(dat->EntryArea);
	GtkTextBuffer* buf = gtk_text_view_get_buffer(tv);
	EmojiPayload* emoji = dat->emoji;

	if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) &&
		!(state & GDK_SHIFT_MASK)) {

		if (emoji->count > 0) {
			EmojiInsert(emoji, g_object_get_data(
								   G_OBJECT(emoji->btns[emoji->sel]), "emoji"));
			return GDK_EVENT_STOP;
		}

		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter(buf, &start);
		gtk_text_buffer_get_end_iter(buf, &end);

		char* content = gtk_text_buffer_get_text(buf, &start, &end, TRUE);

		char* first = content;
		if (curAttachements == NULL) {
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
		}
		PendingSend* ps = calloc(1, sizeof(*ps));
		ps->msg = malloc(sizeof(YampMessage));
		*ps->msg = (YampMessage){0};
		ps->reply = ReplyingToMsg;
		ps->msg->content = strdup(first); // content is freed below
		ps->msg->author = GetUserObject(curUserID);
		ps->msg->reply = isReplying ? ps->reply : NULL;
		ps->msg->timestamp = time(NULL);
		strncpy(ps->channel, currentChannel->id, sizeof(ps->channel) - 1);

		GtkWidget* msgrow = PushUIMessage(chatscroll, chatarea, ps->msg, 1);
		ScrollChatToBottom();
		ps->nonce = RegisterMessageSilhouette(msgrow);

		int n = 0;
		for (AttachementEntry* c = curAttachements; c; c = c->after)
			n++;

		if (n == 0) {
			FinishSend(ps);
		} else {
			ps->msg->attachments = calloc(n, sizeof(YampAttachment*));
			ps->msg->nAttachments = n;
			ps->pending = n;

			int i = 0;
			for (AttachementEntry* cur = curAttachements; cur;
				 cur = cur->after, i++) {
				gchar* data = NULL;
				gsize len = 0;
				GFile* file = g_file_new_for_path(cur->path);
				g_file_load_contents(file, NULL, &data, &len, NULL, NULL);

				gchar* basename = g_file_get_basename(file);
				gboolean uncertain = FALSE;
				gchar* ctype = g_content_type_guess(
					basename, (const guchar*)data, len, &uncertain);
				char* mime = ctype ? g_content_type_get_mime_type(ctype) : NULL;
				if (mime) {
					char* libcmime = strdup(mime);
					g_free(mime);
					mime = libcmime;
					// Go fuck yourself GLib!
				}
				if (!mime)
					mime = strdup("application/octet-stream");

				YampAttachment* att = calloc(1, sizeof *att);
				ps->msg->attachments[i] = att;
				att->mime = mime;
				att->filename = strdup(basename);

				UploadCtx* uc = g_new0(UploadCtx, 1);
				uc->ps = ps;
				uc->att = att;
				YAMPUploadFile(mainsock, mime, data, len,
							   YAMPUploadAttachmentCB, uc);

				g_free(ctype);
				g_free(basename);
				g_object_unref(file);
			}
		}
		gtk_text_buffer_set_text(buf, "", 0);

		free(content);

		if (isReplying) {
			gtk_widget_set_visible(replyindicator, 0);
			isReplying = 0;
		}

		GtkWidget* wdg = gtk_widget_get_first_child(AttachementsBox);
		while (wdg) {
			GtkWidget* next = gtk_widget_get_next_sibling(wdg);
			gtk_widget_unparent(wdg);
			wdg = next;
		}
		gtk_widget_set_visible(AttachementsBox, 0);
		return GDK_EVENT_STOP;
	} else if (keyval == GDK_KEY_colon) {
		if (!emoji->popover)
			EmojiInit(emoji, tv);

		EmojiClose(emoji);

		GtkTextIter it;
		gtk_text_buffer_get_iter_at_mark(buf, &it,
										 gtk_text_buffer_get_insert(buf));

		gboolean ok = gtk_text_iter_is_start(&it);

		if (!ok) {
			GtkTextIter prev = it;
			gtk_text_iter_backward_char(&prev);
			ok = g_unichar_isspace(gtk_text_iter_get_char(&prev));
		}

		if (ok)
			emoji->mark = gtk_text_buffer_create_mark(buf, NULL, &it, TRUE);

		return FALSE;
	} else if (emoji->count > 0 &&
			   (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down ||
				keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up)) {

		gboolean down = (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down);

		EmojiSelect(emoji,
					down ? (emoji->sel + 1) % emoji->count
						 : (emoji->sel + emoji->count - 1) % emoji->count);

		return GDK_EVENT_STOP;
	} else if (keyval == GDK_KEY_Escape && emoji->mark) {
		EmojiClose(emoji);
		return GDK_EVENT_STOP;
	}
	return FALSE;
}