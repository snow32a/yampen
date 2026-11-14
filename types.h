#pragma once
#include <gtk/gtk.h>
typedef struct{
	int index;
	GtkWidget* msgrow;
} MessageSilhouette;


typedef struct AttachementEntry {
	char* path; // heap
	struct AttachementEntry* before;
	struct AttachementEntry* after;
} AttachementEntry;