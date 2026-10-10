#pragma once
#include <glib.h>
#define YAMP_DM 0
#define YAMP_GC 1
#define YAMP_SPACE_TEXT 40
#define YAMP_SPACE_CATEGORY 41
#define YAMP_SPACE_ANNOUNCEMENT 42
#define YAMP_SPACE_INDEX 43
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_fd;
#else
typedef int socket_fd;
#endif
typedef struct {
	char type;		   // 0 = DM, 1 = Guild Channel
	char* where;	   // to be used in the APIs
	char* OtherGuy;	   // for DMs only, otherwise NULL.. CHECK AND DO NOT
					   // DEREFERENCE THAAT!
	char* GuildName;   // Above but for guilds!
	char* ChannelName; // same same, but differeeeent :sob:
} chat;
typedef struct {
	char* status;
	char* RPCName;
	char* RPCDesc;
	char* RPCIcon;
} status;
typedef struct {
	char id[17];
	char* username;
	char* displayname;
	char* description;
	char* pfp;
	status status;
} YampUser;
typedef struct YampChannel {
	int type;
	char id[16+1+16+1];
	char* name;
	struct YampChannel* children;
	int nchildren;
	int pos;
	int npeople;
	YampUser* people;
} YampChannel;
typedef struct {
	char id[17];
	char* name;
	char* displayname;
	char* icon;
	char* banner;
	char* description;
	int type;
} YampSpace;
typedef struct {
    YampUser     usr;
    YampChannel *conversations; int nconversations;
    YampUser    *friends;       int nfriends;
    YampSpace   *spaces;        int nspaces;
    YampUser    *incfq;         int fqcount;
    YampUser    *outfq;         int outfqcount;
} YampLoginData;
#include <openssl/ssl.h>
int YAMPConnect(const char* server, int* fd_out, SSL** socket_out);
int SplitAddress(char* address, char** username, char** server);
int YAMPLogin(SSL* fd, char* username, char* password);
int YAMPRegister(SSL* fd, char* username, char* password);
int YAMPListBuddies(SSL* fd);
int YAMPListConversations(SSL* fd);
int YAMPSendIM(SSL* fd, char* where, char* content);
int YAMPListSpaceChannels(SSL* fd, char* space);
char* MakeDMChannel(const char* a, const char* b);
char* MakeGCChannel(const char* a);
gboolean YAMPProcessWhere(char* where, char* curUsername, chat* out);
int YAMPGetMessageHistory(SSL* fd, char* where);
int YAMPInsertSpace(SSL* fd, char* name, char* display_name, int type,
					char* description, char* banner, char* pfp);
int YAMPCreateChannel(SSL* fd, char* space, char* name, int pos, int type,
					  char* parent);
int YAMPSendFriendReq(SSL* fd, char* to);
int YAMPAcceptFriendReq(SSL* fd, char* user);
int YAMPDenyFriendReq(SSL* fd, char* user);
int YAMPUpdateUserProfile(SSL* fd, YampUser usr);
extern char* YampHTTPAddress;
char* YAMPGetHTTPBaseURI();
int YAMPQueryYAMPHTTP();
int YAMPStartDM(SSL* fd, char* targetusr);
int YAMPCreateGC(SSL* fd, char** init_members, int ninitmem);