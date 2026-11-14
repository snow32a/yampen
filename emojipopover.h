#pragma once
#include <gtk/gtk.h>

typedef struct {
    GtkTextView *tv;
    GtkTextBuffer *buf;
    GtkWidget *popover;
    GtkWidget *grid;
    GtkTextMark *mark;
    GtkWidget *btns[8];
    unsigned int count;
    unsigned int sel;
} EmojiPayload;

void EmojiHide(EmojiPayload *payload);
void EmojiClose(EmojiPayload *payload);
void EmojiSelect(EmojiPayload *payload, guint i);
void EmojiInsert(EmojiPayload *payload, const char *emoji);
int EmojiPopulate(EmojiPayload *payload, const char *query);
char *EmojiQuery(EmojiPayload *payload);
void EmojiUpdate(EmojiPayload *payload);
void EmojiInit(EmojiPayload *payload, GtkTextView *tv);