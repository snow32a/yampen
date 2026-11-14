#include <gtk/gtk.h>
#include "protocol/yamp.h"
#include "types.h"
#include <cjson/cJSON.h>
void StartMainIMWindow(const YampLoginData* dat);
void CreateMainIMWindow(GtkApplication *app);
extern YampChannel* currentChannel;
extern char* curUserID;
extern GtkWidget* chatscroll;
extern GtkWidget* chatarea;
void ScrollChatToBottom(void);
extern GtkWidget* replyindicator;
extern int isReplying;
extern char* ReplyingToMsg;
extern AttachementEntry* curAttachements;
extern GtkWidget* AttachementsBox;
void LoadAvatarAsync(GtkWidget* image, const char* url,
					 const char* cache_path, int px);
char* AvatarCachePath(const char* url);