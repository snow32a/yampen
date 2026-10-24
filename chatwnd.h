#include <gtk-4.0/gtk/gtk.h>
#include "protocol/yamp.h"
GtkWidget* PushUIMessage(GtkWidget* chatscroll, GtkWidget* chatarea,
						 YampMessage* msg, int silhouette);
void ChatAreaSizeChanged(GtkWidget* widget, GParamSpec* pspec, gpointer data);
unsigned int GetDefaultPfp(const char *id);
void DeleteUIMessage(GtkWidget* chatscroll, GtkWidget* chatarea, char* id);
void InitMsgMenus(void);