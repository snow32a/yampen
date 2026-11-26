#pragma once
#include <gtk/gtk.h>
#include <yamp.h>
typedef struct{
	int index;
	GtkWidget* msgrow;
} MessageSilhouette;


typedef struct AttachementEntry {
	char* path; // heap
	struct AttachementEntry* before;
	struct AttachementEntry* after;
} AttachementEntry;


typedef struct {
	char* id;
	char* name;
	guint timeout; // GSource id, 0 if already fired
} TypingEntry;


typedef struct {
    YampChannel **channels;
    guint         count;
} SpaceChannelCache;

typedef struct {
    YampMessage **messages;
    guint         count;
} ChMsgCache;