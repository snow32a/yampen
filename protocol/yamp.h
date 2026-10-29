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
	char* GC_ID; // Do not dereference if its not a GC, because it will be NULL
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
typedef struct YampMessage {
	char id[16+1];
	YampUser author;
	int type; // for later use
	uint64_t timestamp;
	char channel[16+1+16+1];
	char* content;
	struct YampMessage* reply;
} YampMessage;
typedef struct {
    YampUser     usr;
    YampChannel *conversations; int nconversations;
    YampUser    *friends;       int nfriends;
    YampSpace   *spaces;        int nspaces;
    YampUser    *incfq;         int fqcount;
    YampUser    *outfq;         int outfqcount;
} YampLoginData;
#include <openssl/ssl.h>
// connection / addressing
int YAMPConnect(const char* server, int* fd_out, SSL** socket_out);
int SplitAddress(char* address, char** username, char** server);

// auth
int YAMPAuth(SSL* fd, char* token, void* callback);
int YAMPLogin(SSL* fd, char* email, char* password, char* device,
			  int devicetype, void* callback);
int YAMPRegister(SSL* fd, char* email, char* username, char* password,
				 char* device, int devicetype, void* callback);

// friends / profile
int YAMPListBuddies(SSL* fd, void* callback);
int YAMPSendFriendReq(SSL* fd, char* to, void* callback);
int YAMPAcceptFriendReq(SSL* fd, char* user, void* callback);
int YAMPDenyFriendReq(SSL* fd, char* user, void* callback);
int YAMPUpdateUserProfile(SSL* fd, YampUser usr, void* callback);

// conversations / DMs / group chats
int YAMPListConversations(SSL* fd, void* callback);
int YAMPStartDM(SSL* fd, char* targetusr, void* callback);
int YAMPCreateGC(SSL* fd, char** init_members, int ninitmem, void* callback);
char* MakeDMChannel(const char* a, const char* b);
char* MakeGCChannel(const char* a);
const char* ParseGCChannel(const char* a);
gboolean YAMPProcessWhere(char* where, char* curUsername, chat* out);

// messages
int YAMPSendMessage(SSL* fd, char* where, YampMessage* msg, int nonce,
					void* callback);
int YAMPGetMessageHistory(SSL* fd, char* channel, int limit, char* before,
						  void* callback);
int YAMPDeleteMessage(SSL* fd, char* msg_id, void* callback);
int YAMPTyping(SSL* fd, char* channel, void* callback);

// spaces / channels
int YAMPInsertSpace(SSL* fd, char* name, char* display_name, int type,
					char* description, char* banner, char* pfp,
					void* callback);
int YAMPListSpaceChannels(SSL* fd, char* space, void* callback);
int YAMPCreateChannel(SSL* fd, char* space, char* name, int pos, int type,
					  char* parent, void* callback);
int YAMPJoinChannel(SSL* fd, char* channel, void* callback);
int YAMPLeaveChannel(SSL* fd, char* channel, void* callback);

// yamp-http
extern char* YampHTTPAddress;
char* YAMPGetHTTPBaseURI();
int YAMPQueryYAMPHTTP();
int YAMPUploadFile(SSL* fd, char* MIME, unsigned char* file, int len,
				   void* callback);

// status helpers
void YampStatusDupStrings(status* s);
void YampStatusFreeStrings(status* s);

// message object helpers
YampMessage* DupMessageObject(YampMessage* msg);
void FreeMessageObject(YampMessage* msg);

// user object helpers
YampUser YampUserDeepCopy(const YampUser* u);
void YampUserFree(YampUser* u);
YampUser* DupUserType(const YampUser* u);

// channel object helpers
YampChannel YampChannelDup(const YampChannel* s);
YampChannel* DupChannelObject(const YampChannel* s);
void YampChannelFree(YampChannel* ch);


void YAMP_Init(); // SOLELY required due to curl