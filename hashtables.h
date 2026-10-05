#include "protocol/yamp.h"
#include <gtk/gtk.h>
void InitAllTables();
void InsertUserObject(char* id, YampUser* usr);
YampUser* GetUserObject(char* id);
GtkWidget *GetChatWindow(char *forWho);
void RegisterChatWindow(GtkWidget *Window, char *forWho);
void DeregisterChatWindow(char *forWho);
char *InsertProfileDescription(char *username, char *display_name);
char *GetProfileDescription(char *username);
char *InsertPfpPath(char *username, char *display_name);
char *GetPfpPath(char *username);
void InsertSpaceObject(char *id, YampSpace* obj);
YampSpace *GetSpaceObject(char *id);