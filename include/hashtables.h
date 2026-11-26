#include <yamp.h>
#include <gtk/gtk.h>
void InitAllTables();
void InsertUserObject(char* id, YampUser* usr);
YampUser* GetUserObject(char* id);
void InsertConversationObject(char* id, YampChannel* usr);
YampChannel* GetConversationObject(char* id);
GtkWidget *GetChatWindow(char *forWho);
void RegisterChatWindow(GtkWidget *Window, char *forWho);
void DeregisterChatWindow(char *forWho);
void UpdateSpaceChannelCache(YampChannel **channels, int count, const char *space);
int GetSpaceChannelCache(char *space, YampChannel*** out);
int GetChannelMessageCache(char *space, YampMessage*** out);
void InsertPfpPath(char* id, char* path);
char *GetPfpPath(char *username);
void InsertSpaceObject(char *id, YampSpace* obj);
YampSpace *GetSpaceObject(char *id);
void InitMessageSilhouettes(void);
int RegisterMessageSilhouette(GtkWidget *msgrow);
GtkWidget *GetMessageSilhouettes(int index);
