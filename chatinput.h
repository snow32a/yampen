#include <gtk/gtk.h>
#include "emojipopover.h"
typedef struct {
	GtkWidget* EntryArea;
	GtkWidget* ChatView;
    EmojiPayload* emoji;
} EKeyPayload;

int EntryKeyHandler(GtkEventControllerKey *controller, guint keyval,
                    guint keycode, GdkModifierType state,
                    gpointer user_data);

extern int LastTypedAt;