#pragma once
#include <glib.h>
#define YAMP_DM                 0
#define YAMP_GC                 1

#define YAMP_FAMILY_SPACE       (1 << 8)
#define YAMP_FAMILY_MASK        0xFF00
#define YAMP_SUBTYPE_MASK       0x00FF

#define YAMP_SPACE_TEXT         (YAMP_FAMILY_SPACE | 0)
#define YAMP_SPACE_CATEGORY     (YAMP_FAMILY_SPACE | 1)
#define YAMP_SPACE_ANNOUNCEMENT (YAMP_FAMILY_SPACE | 2)
#define YAMP_SPACE_INDEX        (YAMP_FAMILY_SPACE | 3)

#define YAMP_IS_SPACE_TYPE(t)   (((t) & YAMP_FAMILY_MASK) == YAMP_FAMILY_SPACE)

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_fd;
#else
typedef int socket_fd;
#endif
typedef struct {
	int type;
	char* where;	   // to be used in the APIs
	char* OtherGuy;	   // for DMs only, otherwise NULL.. CHECK AND DO NOT
					   // DEREFERENCE THAAT!
	char* GuildName;   // Above but for guilds!
	char* ChannelName; // same same, but differeeeent :sob:
	char* GC_ID; // Do not dereference if its not a GC, because it will be NULL
} chat;


// Attempt to sync these with the server time as much as you can!

typedef struct{
	char* status;
	char* RPCName;
	char* RPCDesc;
	char* RPCIcon;
} YampStatus;

typedef struct {
	char id[17];
	char* username;
	char* displayname;
	char* description;
	char* pfp;
	YampStatus* status;
} YampUser;

typedef struct YampChannel {
	int type;
	char id[16+1+16+1];
	char* name;
	struct YampChannel* children;
	int nchildren;
	int pos;
	int npeople;
	YampUser** people;
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

typedef struct YampMessage YampMessage;
typedef struct{
	char* url;
	char* mime;
	char* filename;
} YampAttachment;
typedef struct{
	char msg_id[17];
	YampUser* user;
	char* codepoint;
} YampReaction;
struct YampMessage {
	char id[16+1];
	YampUser* author;
	int type; // for later use
	uint64_t timestamp;
	char channel[16+1+16+1];
	char* content;
	struct YampMessage* reply;
	YampReaction** reactions;
	int nReactions;
	YampAttachment** attachments; // damn
	int nAttachments;
};

typedef struct {
    YampUser     *usr;
    YampChannel *conversations; int nconversations;
    YampUser    **friends;       int nfriends;
    YampSpace   *spaces;        int nspaces;
    YampUser    **incfq;         int fqcount;
    YampUser    **outfq;         int outfqcount;
} YampLoginData;




typedef void YAMPNewSpaceFn(YampSpace spaces);
typedef void YAMPNewFriendFn(YampUser* friend);
typedef void YAMPNewConversationFn(YampChannel* conversation);
typedef void YAMPChannelsUpdatedFn(YampChannel* channels, int n, char* space);

typedef void YAMPDisconnectedFn(void);
typedef void YAMPStatusUpdateFn(char* name, YampStatus* stat);
typedef void YAMPFriendRequestReceivedFn(char* username);
typedef void YAMPFriendReqSentFn(int success);
typedef void YAMPFriendReqResolvedFn(char* username, int success);
typedef void YAMPReceiveIMFn(YampMessage* msg, int nonce);
typedef void YAMPTypingFn(YampUser* user, char* channelid);
typedef void YAMPMessageDeletedFn(char* message_id);
typedef void YAMPChannelJoinedFn(YampUser* user, char* channel_id);
typedef void YAMPChannelLeaveFn(YampUser* user, char* channel_id);
typedef void YAMPProfileUpdatedFn(YampUser* new_profile);

typedef struct YampEventListener{
    YAMPNewSpaceFn* onNewSpace;
    YAMPNewFriendFn* onNewFriend;
    YAMPNewConversationFn* onNewConversation;
    YAMPChannelsUpdatedFn* onChannelsUpdated;

    YAMPDisconnectedFn* onDisconnected;
    YAMPStatusUpdateFn* onStatusUpdate;
    YAMPFriendRequestReceivedFn* onFriendRequestReceived;
    YAMPFriendReqSentFn* onFriendReqSent;
    YAMPFriendReqResolvedFn* onFriendReqResolved;
    YAMPReceiveIMFn* onReceiveIM;
    YAMPTypingFn* onTyping;
    YAMPMessageDeletedFn* onMessageDeleted;
    YAMPChannelJoinedFn* onChannelJoined;
    YAMPChannelLeaveFn* onChannelLeave;
    YAMPProfileUpdatedFn* onProfileUpdated;

	struct YampEventListener* next;
} YampEventListener;

#include <openssl/ssl.h>
// library stuff
void YAMPRegisterEventListener(YampEventListener* listener);

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
int YAMPUpdateUserProfile(SSL* fd, YampUser* usr, void* callback);

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
int YAMPJoinSpace(SSL* fd, char* code, void* callback);

// yamp-http
extern char* YampHTTPAddress;
char* YAMPGetHTTPBaseURI();
int YAMPQueryYAMPHTTP();
int YAMPUploadFile(SSL* fd, char* MIME, unsigned char* file, int len,
				   void* callback, void* data);

// message object helpers
YampMessage* DupMessageObject(YampMessage* msg);
void FreeMessageObject(YampMessage* msg);

// attachment object helpers
YampAttachment* DupAttachmentObject(YampAttachment* attachment);
void FreeAttachmentTypeFields(YampAttachment* attach);
void FreeAttachmentObject(YampAttachment* attach);

// user object helpers
void FreeUserObject(YampUser* u);
YampUser* DupUserObject(YampUser* usr);

// space object helpers
YampSpace DupSpaceObject(const YampSpace* s);

// channel object helpers
YampChannel* DupChannelObject(const YampChannel* s);
YampChannel DupChannelObject_Inline(const YampChannel* s);
void YampChannelFree(YampChannel* ch);

// status object helpers
void FreeStatusObject(YampStatus* stat);
YampStatus* DupStatusObject(YampStatus* stat);


void YAMP_Init(); // SOLELY required due to curl

char* ProcessAttachmentURL(char* input);

int YAMPCreateSpaceInvite(SSL* fd, char* space, int uses, void* callback, void* userdata);