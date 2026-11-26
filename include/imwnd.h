#include <gtk/gtk.h>
#include <yamp.h>
#include "types.h"
#include <cjson/cJSON.h>
void StartMainIMWindow(const YampLoginData* dat);
void CreateMainIMWindow(GtkApplication *app);
extern YampChannel* currentChannel;
extern char* curUserID;
extern char* curSpace;
extern GtkWidget* chatscroll;
extern GtkWidget* chatarea;
void ScrollChatToBottom(void);
extern GtkWidget* replyindicator;
extern int isReplying;
extern YampMessage* ReplyingToMsg; // YampMessage is already a nice container to store content, id and author here, for ChatView to use
extern AttachementEntry* curAttachements;
extern GtkWidget* AttachementsBox;
extern int MembersListShown;
void LoadAvatarAsync(GtkWidget* image, const char* url,
					 const char* cache_path, int px);
char* AvatarCachePath(const char* url);
void OnChannelSelected(GtkListBox* box, GtkListBoxRow* row,
					   gpointer user_data);
GtkWidget* WrapInStatusFrame(GtkWidget* img, int px, const char* hex);
const char* StatusToColor(const char* statusStr);
GtkWidget* RecursiveFindWidgetByData(GtkWidget* parent, const char* key,
											const char* val);
char* dup0(const char* s);
void TypingClear(void);
int LoadSpaceChannels(YampChannel** channels, int nchannels);