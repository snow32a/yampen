#include "chatinput.h"
#include "protocol/yamp.h"
#include "emojipopover.h"
#include "globals.h"
#include "imwnd.h"
#include "hashtables.h"
#include "chatwnd.h"
int LastTypedAt = 0;

int EntryKeyHandler(GtkEventControllerKey *controller, guint keyval,
                    guint keycode, GdkModifierType state,
                    gpointer user_data) {
    if(time(NULL)-LastTypedAt>5 && currentChat){
        YAMPTyping(mainsock, currentChat);
    }
    EKeyPayload *dat = user_data;
    GtkTextView *tv = GTK_TEXT_VIEW(dat->EntryArea);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    EmojiPayload *emoji = dat->emoji;

    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) &&
        !(state & GDK_SHIFT_MASK)) {

        if (emoji->count > 0) {
            EmojiInsert(
                emoji,
                g_object_get_data(
                    G_OBJECT(emoji->btns[emoji->sel]), "emoji"
                )
            );
            return GDK_EVENT_STOP;
        }

        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(buf, &start);
        gtk_text_buffer_get_end_iter(buf, &end);

        char *content = gtk_text_buffer_get_text(
            buf, &start, &end, TRUE
        );

        char *first = content;
        while (*first && g_ascii_isspace((guchar)*first))
            first++;

        char *last = content + strlen(content);
        while (last > first && g_ascii_isspace((guchar)last[-1]))
            last--;

        if (first == last) {
            g_free(content);
            return GDK_EVENT_STOP;
        }

        *last = '\0';
        YampMessage replymsg = {0};
        if(isReplying){
            strcpy(replymsg.id,ReplyingToMsg);
        }
        YampMessage msg;
        msg.content = first;
        msg.author = *GetUserObject(curUserID);
        msg.reply = isReplying ? &replymsg : NULL;
        msg.type = 0;
        msg.timestamp = time(NULL);
        GtkWidget* msgrow = PushUIMessage(chatscroll, chatarea, &msg,1);
        ScrollChatToBottom();
        int nonce = RegisterMessageSilhouette(msgrow);
        YAMPSendMessage(mainsock, currentChat, &msg, nonce);
        gtk_text_buffer_set_text(buf, "", 0);

        g_free(content);

    if(isReplying){
	    gtk_widget_set_visible(replyindicator, 0);
        isReplying = 0;
    }
        return GDK_EVENT_STOP;

    } else if (keyval == GDK_KEY_colon) {
        if (!emoji->popover)
            EmojiInit(emoji, tv);

        EmojiClose(emoji);

        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(
            buf, &it, gtk_text_buffer_get_insert(buf)
        );

        gboolean ok = gtk_text_iter_is_start(&it);

        if (!ok) {
            GtkTextIter prev = it;
            gtk_text_iter_backward_char(&prev);
            ok = g_unichar_isspace(
                gtk_text_iter_get_char(&prev)
            );
        }

        if (ok)
            emoji->mark = gtk_text_buffer_create_mark(
                buf, NULL, &it, TRUE
            );

        return FALSE;

    } else if (emoji->count > 0 &&
               (keyval == GDK_KEY_Down ||
                keyval == GDK_KEY_KP_Down ||
                keyval == GDK_KEY_Up ||
                keyval == GDK_KEY_KP_Up)) {

        gboolean down = (
            keyval == GDK_KEY_Down ||
            keyval == GDK_KEY_KP_Down
        );

        EmojiSelect(
            emoji,
            down
                ? (emoji->sel + 1) % emoji->count
                : (emoji->sel + emoji->count - 1) % emoji->count
        );

        return GDK_EVENT_STOP;

    } else if (keyval == GDK_KEY_Escape && emoji->mark) {
        EmojiClose(emoji);
        return GDK_EVENT_STOP;
    }
    return FALSE;
}