#include <gtk/gtk.h>
#include "emoji_map.h"

typedef struct {
    GtkTextView *tv;
    GtkTextBuffer *buf;
    GtkWidget *popover;
    GtkWidget *grid;
    GtkTextMark *mark;
    GtkWidget *btns[8];
    unsigned int count; // if this is 0 the thing isn't there
    unsigned int sel;
} EmojiPayload;

void EmojiHide(EmojiPayload *payload) {
    payload->count = 0;

    if (payload->popover)
        gtk_popover_popdown(GTK_POPOVER(payload->popover));
}

void EmojiClose(EmojiPayload *payload) {
    EmojiHide(payload);

    if (payload->mark) {
        gtk_text_buffer_delete_mark(payload->buf, payload->mark);
        payload->mark = NULL;
    }
}

void EmojiSelect(EmojiPayload *payload, guint i) {
    if (payload->count == 0 || i >= payload->count)
        return;

    if (payload->sel < payload->count)
        gtk_widget_remove_css_class(
            payload->btns[payload->sel], "emoji-sel"
        );

    payload->sel = i;

    gtk_widget_add_css_class(
        payload->btns[payload->sel], "emoji-sel"
    );
}

void EmojiInsert(EmojiPayload *payload, const char *emoji) {
    if (!payload->mark ||
        gtk_text_mark_get_deleted(payload->mark) ||
        !emoji)
        return;

    GtkTextIter s, e;

    gtk_text_buffer_get_iter_at_mark(
        payload->buf, &s, payload->mark
    );

    gtk_text_buffer_get_iter_at_mark(
        payload->buf, &e,
        gtk_text_buffer_get_insert(payload->buf)
    );

    // Drop the mark first so the changed handler ignores our edit.
    EmojiClose(payload);

    gtk_text_buffer_begin_user_action(payload->buf);

    gtk_text_buffer_delete(payload->buf, &s, &e);
    gtk_text_buffer_insert(payload->buf, &s, emoji, -1);

    gtk_text_buffer_end_user_action(payload->buf);
}

void EmojiPick(GtkButton *btn, gpointer data) {
    EmojiPayload *payload = data;

    const char *emoji = g_object_get_data(
        G_OBJECT(btn), "emoji"
    );

    EmojiInsert(payload, emoji);
}

int EmojiPopulate(EmojiPayload *payload, const char *query) {
    GtkWidget *c;

    while ((c = gtk_widget_get_first_child(payload->grid)))
        gtk_grid_remove(GTK_GRID(payload->grid), c);

    payload->count = 0;

    char *needle = g_ascii_strdown(query, -1);
    int n = 0;

    for (int i = 0; i < n_emoji_map && n < 8; i++) {
        if (strncmp(
                emoji_map[i].name,
                needle,
                strlen(needle)
            ) != 0)
            continue;

        GtkWidget *box = gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL, 8
        );

        GtkWidget *em = gtk_label_new(emoji_map[i].emoji);
        GtkWidget *lb = gtk_label_new(emoji_map[i].name);

        gtk_label_set_xalign(GTK_LABEL(lb), 0);

        gtk_box_append(GTK_BOX(box), em);
        gtk_box_append(GTK_BOX(box), lb);

        GtkWidget *btn = gtk_button_new();

        gtk_button_set_child(GTK_BUTTON(btn), box);
        gtk_widget_add_css_class(btn, "flat");
        gtk_widget_set_focusable(btn, FALSE);
        gtk_widget_set_focus_on_click(btn, FALSE);

        g_object_set_data(
            G_OBJECT(btn),
            "emoji",
            (gpointer)emoji_map[i].emoji
        );

        g_signal_connect(
            btn,
            "clicked",
            G_CALLBACK(EmojiPick),
            payload
        );

        gtk_grid_attach(
            GTK_GRID(payload->grid),
            btn,
            n % 2,
            n / 2,
            1,
            1
        );

        payload->btns[n++] = btn;
    }

    g_free(needle);

    payload->count = n;
    payload->sel = 0;

    if (n)
        gtk_widget_add_css_class(
            payload->btns[0], "emoji-sel"
        );

    return n;
}

char *EmojiQuery(EmojiPayload *payload) {
    if (!payload->mark ||
        gtk_text_mark_get_deleted(payload->mark))
        return NULL;

    GtkTextIter colon, from, cur;

    gtk_text_buffer_get_iter_at_mark(
        payload->buf, &colon, payload->mark
    );

    gtk_text_buffer_get_iter_at_mark(
        payload->buf, &cur,
        gtk_text_buffer_get_insert(payload->buf)
    );

    if (gtk_text_iter_get_char(&colon) != ':' ||
        gtk_text_iter_compare(&cur, &colon) <= 0)
        return NULL;

    from = colon;
    gtk_text_iter_forward_char(&from);

    char *q = gtk_text_buffer_get_text(
        payload->buf, &from, &cur, FALSE
    );

    for (const char *p = q; *p; p++) {
        if (g_ascii_isspace(*p) || *p == ':') {
            g_free(q);
            return NULL;
        }
    }

    return q;
}

void EmojiUpdate(EmojiPayload *payload) {
    if (!payload->mark)
        return;

    char *q = EmojiQuery(payload);

    if (!q) {
        EmojiClose(payload);
        return;
    }

    if (g_utf8_strlen(q, -1) < 2 ||
        EmojiPopulate(payload, q) == 0) {
        EmojiHide(payload);
        g_free(q);
        return;
    }

    g_free(q);

    GtkTextIter cur;

    gtk_text_buffer_get_iter_at_mark(
        payload->buf, &cur,
        gtk_text_buffer_get_insert(payload->buf)
    );

    GdkRectangle r;

    gtk_text_view_get_iter_location(
        payload->tv, &cur, &r
    );

    gtk_text_view_buffer_to_window_coords(
        payload->tv,
        GTK_TEXT_WINDOW_WIDGET,
        r.x, r.y,
        &r.x, &r.y
    );

    gtk_popover_set_pointing_to(
        GTK_POPOVER(payload->popover), &r
    );

    gtk_popover_popup(GTK_POPOVER(payload->popover));
}

void EmojiOnChanged(GtkTextBuffer *buf, gpointer data) {
    EmojiPayload *payload = data;
    EmojiUpdate(payload);
}

void EmojiOnCursor(GObject *obj, GParamSpec *pspec, gpointer data) {
    EmojiPayload *payload = data;
    EmojiUpdate(payload);
}

void EmojiInit(EmojiPayload *payload, GtkTextView *tv) {
    payload->tv = tv;
    payload->buf = gtk_text_view_get_buffer(tv);

    payload->popover = gtk_popover_new();

    gtk_widget_set_parent(
        payload->popover,
        gtk_widget_get_parent(GTK_WIDGET(tv))
    );

    gtk_popover_set_autohide(
        GTK_POPOVER(payload->popover), FALSE
    ); // no grab

    gtk_popover_set_has_arrow(
        GTK_POPOVER(payload->popover), FALSE
    );

    gtk_popover_set_position(
        GTK_POPOVER(payload->popover), GTK_POS_TOP
    );

    gtk_widget_set_focusable(payload->popover, FALSE);

    payload->grid = gtk_grid_new();

    gtk_grid_set_column_spacing(
        GTK_GRID(payload->grid), 4
    );

    gtk_grid_set_row_spacing(
        GTK_GRID(payload->grid), 4
    );

    gtk_grid_set_column_homogeneous(
        GTK_GRID(payload->grid), TRUE
    );

    gtk_popover_set_child(
        GTK_POPOVER(payload->popover), payload->grid
    );

    payload->mark = NULL;
    payload->count = 0;
    payload->sel = 0;

    g_signal_connect(
        payload->buf,
        "changed",
        G_CALLBACK(EmojiOnChanged),
        payload
    );

    g_signal_connect(
        payload->buf,
        "notify::cursor-position",
        G_CALLBACK(EmojiOnCursor),
        payload
    );
}