#include <gtk-4.0/gtk/gtk.h>
void PushUIMessage(GtkWidget* chatscroll, GtkWidget* chatarea, char* userid,
				   char* displayname, char* pfppath, char* content);
void ChatAreaSizeChanged(GtkWidget* widget, GParamSpec* pspec, gpointer data);
unsigned int GetDefaultPfp(const char *id);