#include <glib.h>
#define YAMP_GUILD 1
#define YAMP_DM 0

typedef struct {
	char type; // 0 = DM, 1 = Guild Channel
	char *where; // to be used in the APIs
	char *OtherGuy; // for DMs only, otherwise NULL.. CHECK AND DO NOT
	                // DEREFERENCE THAAT!
	char *GuildName; // Above but for guilds!
	char *ChannelName; // same same, but differeeeent :sob:
} chat;

int YAMPConnect(const char *server, int *socket_out);
int SplitAddress(char *address, char **username, char **server);
int YAMPLogin(int fd, char *username, char *password);
int YAMPListBuddies(int fd);
int YAMPSendIM(int fd, char *where, char *content);
int YAMPListSpaceChannels(int fd, char *space);
char *MakeDMChannel(const char *a, const char *b);
gboolean YAMPProcessWhere(char *where, char *curUsername, chat* out);
int YAMPGetMessageHistory(int fd, char *where);
