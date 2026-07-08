#include <gtk/gtk.h>
void InitAllTables();
char *InsertDisplayName(char *username, char *display_name);
char *GetDisplayName(char *username);
GtkWidget *GetChatWindow(char *forWho);
void RegisterChatWindow(GtkWidget *Window, char *forWho);
void DeregisterChatWindow(char *forWho);
char *InsertProfileDescription(char *username, char *display_name);
char *GetProfileDescription(char *username);
char *InsertPfpPath(char *username, char *display_name);
char *GetPfpPath(char *username);
