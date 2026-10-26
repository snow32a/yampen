#include "cjson/cJSON.h"
#include "glib.h"
#include "glibconfig.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/ssl.h>
char* YampHTTPAddress;
#include "yamp.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_fd;
#else
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#endif
#define YAMP_PORT 5225
extern void onYAMPFriendsListed(YampUser* friends, int nfriends);
extern void onYAMPConversationsListed(YampUser* friends, int nconv);
extern void onYAMPUserDetailsFetched(cJSON* Detail);
extern void onYAMPSpacesFetched(YampSpace* spaces, int nspaces);
extern void onYAMPNewSpace(YampSpace spaces);
extern void onYAMPNewFriend(YampUser friend);
extern void onYAMPChannelsFetched(YampChannel* channels, int n);
extern void onYAMPChannelsUpdated(YampChannel* channels, int n, char* space);
extern void onYAMPLoggedIn(YampLoginData* payload);
extern void onYAMPLoginFail();
extern void onYAMPDisconnected();
extern void onYAMPStatusUpdate(char* name, status stat);
extern void onYAMPFriendRequestReceived(char* username);
extern void onYAMPFriendReqSent(int success);
extern void onYAMPRegisterResult(int success);
extern void onYAMPFriendReqResolved(char* username, int success);
extern void onYAMPReceiveIM(YampMessage* msg, int nonce);
extern void onYAMPTyping(YampUser user, char* channelid);
extern void onYAMPMessageDeleted(char* message_id);
extern void onYAMPChannelJoined(YampUser* user, char* channel_id);
extern void onYAMPChannelLeave(YampUser* user, char* channel_id);
extern void onYAMPMessageHistoryLoaded(YampMessage** msgs, int nmsgs, int hasmore);

static char* safe_strdup(const char* s) { return s ? strdup(s) : NULL; }
static void CopyID(char* dst, size_t cap, const char* src) {
	strncpy(dst, src ? src : "", cap);
}
void YampStatusDupStrings(status* s) {
	s->status = safe_strdup(s->status);
	s->RPCName = safe_strdup(s->RPCName);
	s->RPCDesc = safe_strdup(s->RPCDesc);
	s->RPCIcon = safe_strdup(s->RPCIcon);
}
void YampStatusFreeStrings(status* s) {
	free(s->status);
	free(s->RPCName);
	free(s->RPCDesc);
	free(s->RPCIcon);
}

void YampUserDupStrings(YampUser* u) {
	u->username = safe_strdup(u->username);
	u->displayname = safe_strdup(u->displayname);
	u->description = safe_strdup(u->description);
	u->pfp = safe_strdup(u->pfp);
	YampStatusDupStrings(&u->status);
}
YampUser YampUserClone(const YampUser* u) { return *u; } /* strings borrowed */
YampUser YampUserDeepCopy(const YampUser* u) {
	YampUser c = *u;
	YampUserDupStrings(&c);
	return c;
}
YampUser* DupUserType(const YampUser* u) {
	YampUser* c = malloc(sizeof(YampUser));
	*c = *u;
	YampUserDupStrings(c);
	return c;
}
/******************************************************
 * YampUserFreeStrings
 * Frees the strings in a YAMP user
 * @param YampUser* u

******************************************************/
void YampUserFreeStrings(YampUser* u) {
	free(u->username);
	free(u->displayname);
	free(u->description);
	free(u->pfp);
	YampStatusFreeStrings(&u->status);
}
void YampUserArrayFreeStringless(YampUser* a) { free(a); }
void YampUserFree(YampUser* u) {
	YampUserFreeStrings(u);
	free(u);
}

YampUser* YampUserArrayDeepCopy(const YampUser* a, int n) {
	YampUser* r = malloc(sizeof(YampUser) * (n ? n : 1));
	for (int i = 0; i < n; i++)
		r[i] = YampUserDeepCopy(&a[i]);
	return r;
}
void YampUserArrayFree(YampUser* a, int n) {
	if (!a)
		return;
	for (int i = 0; i < n; i++)
		YampUserFreeStrings(&a[i]);
	free(a);
}



void YampSpaceDupStrings(YampSpace* s) {
	s->name = safe_strdup(s->name);
	s->displayname = safe_strdup(s->displayname);
	s->description = safe_strdup(s->description);
	s->banner = safe_strdup(s->banner);
	s->icon = safe_strdup(s->icon);
}
YampSpace YampSpaceDupStringless(const YampSpace* s) { return *s; }
YampSpace YampSpaceDup(const YampSpace* s) {
	YampSpace c = *s;
	YampSpaceDupStrings(&c);
	return c;
}
void YampSpaceFreeStrings(YampSpace* s) {
	free(s->name);
	free(s->displayname);
	free(s->description);
	free(s->banner);
	free(s->icon);
}
YampSpace* YampSpaceArrayDup(const YampSpace* a, int n) {
	YampSpace* r = malloc(sizeof(YampSpace) * (n ? n : 1));
	for (int i = 0; i < n; i++)
		r[i] = YampSpaceDup(&a[i]);
	return r;
}
void YampSpaceArrayFree(YampSpace* a, int n) {
	if (!a)
		return;
	for (int i = 0; i < n; i++)
		YampSpaceFreeStrings(&a[i]);
	free(a);
}



char* MakeDMChannel(const char* a, const char* b) {
	if (strcmp(a, b) < 0)
		return g_strdup_printf("%s|%s", a, b);
	else
		return g_strdup_printf("%s|%s", b, a);
}
char* MakeGCChannel(const char* a) {
	char* ret = malloc(strlen(a) + 2);
	strcpy(ret + 1, a);
	ret[0] = '&';
	return ret;
}
const char* ParseGCChannel(const char* a) { return a + 1; }

////////////////////////////////////////////////
///      YAMP'S WHERE PARAMETER STYLE        ///
/// SPACES:     DMs:                GCs:     ///
/// #channel    userID1-userID2     &GC      ///
////////////////////////////////////////////////
int YAMPProcessWhere(char* where, char* curUserID, chat* out) {
	chat retval = {0};
	if (*where == '&') {
		// Group chat
		retval.GC_ID = strdup(where + 1);
		retval.OtherGuy = NULL;
		retval.type = YAMP_GC;
		retval.where = strdup(where);
		*out = retval;
		return TRUE;
	} else {
		// Could be a damn DM?
		char* firstUsr = strdup(where);
		char* minus = strchr(firstUsr, '|');
		if (!minus) {
			// This means it's a space channel!
			free(firstUsr);
			retval.GuildName = NULL; // long deprecated sorry
			retval.ChannelName = strdup(where);
			retval.OtherGuy = NULL;
			retval.type = YAMP_SPACE_TEXT;
			retval.where = strdup(where);
			*out = retval;
			return TRUE;
		}
		*minus = '\0';
		retval.ChannelName = NULL;
		retval.type = YAMP_DM;
		retval.where = strdup(where);
		if (strcmp(firstUsr, curUserID) == 0) {
			retval.OtherGuy = minus + 1;
		} else if (strcmp(minus + 1, curUserID) == 0) {
			retval.OtherGuy = firstUsr;
		} else {
			return FALSE; // who the FUCK are you
		}
		retval.GuildName = NULL;
		*out = retval;
		return TRUE;
	}
}

int TLSYAMPSend(SSL* fd, void* payload, uint32_t size) {
	uint32_t NlSize = htonl(size);
	SSL_write(fd, &NlSize, 4);
	return SSL_write(fd, payload, size);
}
int TLSYAMPRecv(SSL* fd, char** payload, uint32_t* len) {
	if (SSL_read(fd, len, 4) > 0) {
		*len = ntohl(*len);
		*payload = malloc(*len + 1);
		int totalread = 0;
		while (totalread < *len) {
			int r = SSL_read(fd, *payload + totalread, *len - totalread);
			totalread += r;
		}
		(*payload)[*len] = '\0';
		return 1;
	}
	return 0;
}

static char* JsonStrOrNull(cJSON* obj, const char* key) {
	cJSON* item = cJSON_GetObjectItem(obj, key);
	return cJSON_IsString(item) ? item->valuestring : NULL;
}

YampSpace ParseSpaceObject(cJSON* raw) {
	YampSpace sp = {0};

	char* id = JsonStrOrNull(raw, "id");
	if (id)
		CopyID(sp.id, sizeof sp.id, JsonStrOrNull(raw, "id"));

	sp.name = safe_strdup(JsonStrOrNull(raw, "name"));
	sp.displayname = safe_strdup(JsonStrOrNull(raw, "display_name"));
	sp.description = safe_strdup(JsonStrOrNull(raw, "description"));
	sp.banner = safe_strdup(JsonStrOrNull(raw, "banner"));
	sp.icon = safe_strdup(JsonStrOrNull(raw, "icon"));

	cJSON* type = cJSON_GetObjectItem(raw, "type");
	if (!type)
		type = cJSON_GetObjectItem(raw, "space_type");
	sp.type = cJSON_IsNumber(type) ? type->valueint : 0;

	return sp;
}
YampUser ParseUserObject(cJSON* rawusr) {
	YampUser usr = {0};
	CopyID(usr.id, sizeof usr.id, JsonStrOrNull(rawusr, "id"));
	usr.username = safe_strdup(JsonStrOrNull(rawusr, "name"));
	usr.displayname = safe_strdup(JsonStrOrNull(rawusr, "display_name"));
	usr.description = safe_strdup(JsonStrOrNull(rawusr, "description"));
	usr.pfp = safe_strdup(JsonStrOrNull(rawusr, "pfp"));

	cJSON* st = cJSON_GetObjectItem(rawusr, "status");
	usr.status.status = safe_strdup(JsonStrOrNull(st, "status"));
	usr.status.RPCName = safe_strdup(JsonStrOrNull(st, "RPCName"));
	usr.status.RPCDesc = safe_strdup(JsonStrOrNull(st, "RPCDesc"));
	usr.status.RPCIcon = safe_strdup(JsonStrOrNull(st, "RPCIcon"));
	return usr;
}
YampChannel RecursiveParseChannel(cJSON* raw) {
	YampChannel ret = {0};
	cJSON* pos = cJSON_GetObjectItem(raw, "position");
	if (pos) {
		ret.pos = pos->valueint;
	}
	ret.type = cJSON_GetObjectItem(raw, "type")->valueint;
	cJSON* name = cJSON_GetObjectItem(raw, "name");
	if (name) {
		ret.name = safe_strdup(name->valuestring);
	}
	strcpy(ret.id, cJSON_GetObjectItem(raw, "id")->valuestring);
	cJSON* children = cJSON_GetObjectItem(raw, "children");
	if (children) {
		ret.children =
			malloc(cJSON_GetArraySize(children) * sizeof(YampChannel));
		ret.nchildren = cJSON_GetArraySize(children);
		for (int i = 0; i < cJSON_GetArraySize(children); i++) {
			ret.children[i] =
				RecursiveParseChannel(cJSON_GetArrayItem(children, i));
		}
	} else {
		ret.children = NULL;
		ret.nchildren = 0;
	}

	ret.type = cJSON_GetObjectItem(raw, "type")->valueint;
	const cJSON* jpeople = cJSON_GetObjectItem(raw, "members");
	const char* addr = cJSON_GetStringValue(cJSON_GetObjectItem(raw, "id"));
	ret.npeople = cJSON_GetArraySize(jpeople);
	ret.people = malloc(sizeof(YampUser) * cJSON_GetArraySize(jpeople));
	int i = 0;
	const cJSON* item;
	cJSON_ArrayForEach(item, jpeople) {
		ret.people[i++] = ParseUserObject(item);
	}

	return ret;
}

YampChannel YampChannelDup(const YampChannel* s) {
	YampChannel c = *s;

	c.name = safe_strdup(s->name);

	if (s->children && s->nchildren > 0) {
		c.children = malloc(sizeof(YampChannel) * s->nchildren);
		for (int i = 0; i < s->nchildren; i++)
			c.children[i] = YampChannelDup(&s->children[i]);
	} else {
		c.children = NULL;
		c.nchildren = 0;
	}

	if (s->people && s->npeople > 0) {
		c.people = YampUserArrayDeepCopy(s->people, s->npeople);
	} else {
		c.people = NULL;
		c.npeople = 0;
	}

	return c;
}

YampChannel* DupChannelObject(const YampChannel* s) {
	YampChannel* c = malloc(sizeof(YampChannel));
	*c = YampChannelDup(s);
	return c;
}
void YampChannelFree(YampChannel* ch) {
	if (!ch)
		return;

	free(ch->name);

	for (int i = 0; i < ch->nchildren; i++)
		YampChannelFree(&ch->children[i]);
	free(ch->children);
	ch->children = NULL;
	ch->nchildren = 0;

	YampUserArrayFree(ch->people, ch->npeople);
	ch->people = NULL;
	ch->npeople = 0;
}
cJSON* CreateUserObject(YampUser user) {
	cJSON* returnObj = cJSON_CreateObject();

	cJSON_AddStringToObject(returnObj, "id", user.id);
	cJSON_AddStringToObject(returnObj, "name", user.username);

	if (user.displayname)
		cJSON_AddStringToObject(returnObj, "display_name", user.displayname);

	if (user.description)
		cJSON_AddStringToObject(returnObj, "description", user.description);

	if (user.pfp)
		cJSON_AddStringToObject(returnObj, "pfp", user.pfp);

	if (user.status.RPCName || user.status.RPCDesc || user.status.RPCIcon ||
		user.status.status) {

		cJSON* statusObj = cJSON_CreateObject();

		if (user.status.RPCName)
			cJSON_AddStringToObject(statusObj, "RPCName", user.status.RPCName);

		if (user.status.RPCDesc)
			cJSON_AddStringToObject(statusObj, "RPCDesc", user.status.RPCDesc);

		if (user.status.RPCIcon)
			cJSON_AddStringToObject(statusObj, "RPCIcon", user.status.RPCIcon);

		if (user.status.status)
			cJSON_AddStringToObject(statusObj, "status", user.status.status);

		cJSON_AddItemToObject(returnObj, "status", statusObj);
	}

	return returnObj;
}
void FreeMessageObject(YampMessage* msg) {
	free(msg->content);
	if (msg->reply)
		FreeMessageObject(msg->reply);
	free(msg);
}
YampMessage* ParseMessageObject(cJSON* object) {
	YampMessage* msg = malloc(sizeof(YampMessage));
	*msg = (YampMessage){0};
	msg->author = ParseUserObject(cJSON_GetObjectItem(object, "author"));
	msg->content = strdup(cJSON_GetObjectItem(object, "content")->valuestring);
	strncpy(msg->channel, cJSON_GetObjectItem(object, "channel")->valuestring,
			34);
	if (cJSON_GetObjectItem(object, "reply")) {
		msg->reply = ParseMessageObject(cJSON_GetObjectItem(object, "reply"));
	}
	strncpy(msg->id, cJSON_GetObjectItem(object, "id")->valuestring, 17);
	msg->timestamp = cJSON_GetObjectItem(object, "timestamp")->valueint;
	msg->type = cJSON_GetObjectItem(object, "type")->valueint;
	return msg;
}
YampMessage* DupMessageObject(YampMessage* msg){
	YampMessage* heapmsg = malloc(sizeof(YampMessage));
	*heapmsg=*msg;
	heapmsg->content=strdup(msg->content);
	heapmsg->author = YampUserDeepCopy(&heapmsg->author);
	if(msg->reply){
		heapmsg->reply = DupMessageObject(msg->reply);
	}
	return heapmsg;
}

void* YAMPRecvLoop(void* fd) {
	uint32_t len;
	char* payload;
	while (1) {
		if (TLSYAMPRecv(fd, &payload, &len)) {
			cJSON* srvr = cJSON_Parse(payload);
			cJSON* type = cJSON_GetObjectItem(srvr, "type");
			int success = cJSON_IsTrue(cJSON_GetObjectItem(srvr, "success"));
			if (strcmp(type->valuestring, "response") == 0) {
				cJSON* reqid = cJSON_GetObjectItem(srvr, "reqid");
				cJSON* response = cJSON_GetObjectItem(srvr, "response");
				if (strcmp(reqid->valuestring, "1") == 0) {
					printf("FRIENDS LISTED\n");
					YampUser* friends =
						malloc(cJSON_GetArraySize(response) * sizeof(YampUser));
					cJSON* rawfriends =
						cJSON_GetObjectItem(response, "friends");
					for (int i = 0; i < cJSON_GetArraySize(rawfriends); i++) {
						friends[i] =
							ParseUserObject(cJSON_GetArrayItem(rawfriends, i));
					}
					onYAMPFriendsListed(friends,
										cJSON_GetArraySize(rawfriends));
				} else if (strcmp(reqid->valuestring, "Register") == 0) {
					printf("REGISTER RESP\n");
					onYAMPRegisterResult(success);
				} else if (strcmp(reqid->valuestring, "0") == 0) {
					printf("LOGIN RESP\n");
					if (success) {
						cJSON* jsonusr = cJSON_GetObjectItem(response, "user");
						cJSON* raw_incoming_fq =
							cJSON_GetObjectItem(response, "incoming_fq");
						cJSON* raw_outgoing_fq =
							cJSON_GetObjectItem(response, "outgoing_fq");
						YampUser* incoming_fq =
							malloc(sizeof(YampUser) *
								   cJSON_GetArraySize(raw_incoming_fq));
						YampUser* outgoing_fq =
							malloc(sizeof(YampUser) *
								   cJSON_GetArraySize(raw_outgoing_fq));
						for (int i = 0; i < cJSON_GetArraySize(raw_incoming_fq);
							 i++) {
							incoming_fq[i] = ParseUserObject(
								cJSON_GetArrayItem(raw_incoming_fq, i));
						}
						for (int i = 0; i < cJSON_GetArraySize(raw_outgoing_fq);
							 i++) {
							outgoing_fq[i] = ParseUserObject(
								cJSON_GetArrayItem(raw_outgoing_fq, i));
						}
						YampUser usr = ParseUserObject(jsonusr);
						cJSON* rawspaces =
							cJSON_GetObjectItem(response, "spaces");
						int nspaces = cJSON_IsArray(rawspaces)
										  ? cJSON_GetArraySize(rawspaces)
										  : 0;
						YampSpace* spaces =
							malloc(sizeof(YampSpace) * (nspaces ? nspaces : 1));
						for (int i = 0; i < nspaces; i++) {
							spaces[i] = ParseSpaceObject(
								cJSON_GetArrayItem(rawspaces, i));
						}
						if (cJSON_GetObjectItem(response, "yamp-http")) {
							YampHTTPAddress =
								cJSON_GetObjectItem(response, "yamp-http")
									->valuestring;
						} else {
							YampHTTPAddress = NULL;
						}
						cJSON* rawconvs =
							cJSON_GetObjectItem(response, "conversations");
						int nconvs = cJSON_IsArray(rawconvs)
										 ? cJSON_GetArraySize(rawconvs)
										 : 0;
						YampChannel* convs =
							malloc(sizeof(YampChannel) * (nconvs ? nconvs : 1));
						for (int i = 0; i < nconvs; i++) {
							convs[i] = RecursiveParseChannel(
								cJSON_GetArrayItem(rawconvs, i));
						}
						cJSON* rawfriends =
							cJSON_GetObjectItem(response, "friends");
						int nfriends = cJSON_IsArray(rawfriends)
										   ? cJSON_GetArraySize(rawfriends)
										   : 0;
						YampUser* friends = malloc(sizeof(YampUser) *
												   (nfriends ? nfriends : 1));
						for (int i = 0; i < nfriends; i++) {
							friends[i] = ParseUserObject(
								cJSON_GetArrayItem(rawfriends, i));
						}
						YampLoginData dat;
						dat.usr = usr;
						dat.conversations = convs;
						dat.nconversations = nconvs;
						dat.friends = friends;
						dat.nfriends = nfriends;
						dat.spaces = spaces;
						dat.nspaces = nspaces;
						dat.incfq = incoming_fq;
						dat.fqcount = cJSON_GetArraySize(raw_incoming_fq);
						dat.outfq = outgoing_fq;
						dat.outfqcount = cJSON_GetArraySize(raw_outgoing_fq);
						YampLoginData* heapdat = malloc(sizeof(YampLoginData));
						*heapdat = dat;
						onYAMPLoggedIn(heapdat);
					} else {
						onYAMPLoginFail();
					}
				} else if (*(reqid->valuestring) == '2') {
					cJSON* channelarr =
						cJSON_GetObjectItem(response, "channels");
					YampChannel* charr = malloc(sizeof(YampChannel) *
												cJSON_GetArraySize(channelarr));
					for (int i = 0; i < cJSON_GetArraySize(channelarr); i++) {
						charr[i] = RecursiveParseChannel(
							cJSON_GetArrayItem(channelarr, i));
					}
					onYAMPChannelsFetched(charr,
										  cJSON_GetArraySize(channelarr));
				} else if (strcmp(reqid->valuestring, "GetMessageHistory") ==
						   0) {
					cJSON* msgarr = cJSON_GetObjectItem(response, "messages");
					YampMessage** msgs = malloc(sizeof(YampMessage**)*cJSON_GetArraySize(msgarr));
					for (int i = 0; i < cJSON_GetArraySize(msgarr); i++) {
						cJSON* cmsg = cJSON_GetArrayItem(msgarr, i);
						msgs[i] = ParseMessageObject(cmsg);
					}
					onYAMPMessageHistoryLoaded(msgs, cJSON_GetArraySize(msgarr), cJSON_GetObjectItem(response, "has_more")->valueint);
					for (int i = 0; i < cJSON_GetArraySize(msgarr); i++) {
						FreeMessageObject(msgs[i]);
					}
				} else if (strcmp(reqid->valuestring, "SendFriendReq") == 0) {
					onYAMPFriendReqSent(success);
				} else if (strncmp(reqid->valuestring, "AcceptFriendReq|",
								   16) == 0) {
					onYAMPFriendReqResolved(reqid->valuestring + 16, success);
				} else if (strncmp(reqid->valuestring, "DenyFriendReq|", 14) ==
						   0) {
					onYAMPFriendReqResolved(reqid->valuestring + 14, success);
				}
			} else if (strcmp(type->valuestring, "event") == 0) {
				cJSON* event = cJSON_GetObjectItem(srvr, "event");
				cJSON* eventdata = cJSON_GetObjectItem(srvr, "data");
				if (strcmp(event->valuestring, "recvim") == 0) {
					cJSON* cnonce = cJSON_GetObjectItem(eventdata, "nonce");
					int nonce = -1;
					if (cnonce) {
						nonce = cJSON_GetNumberValue(cnonce);
					}
					cJSON* cmsg = cJSON_GetObjectItem(eventdata, "message");
					YampMessage* msg = ParseMessageObject(cmsg);
					onYAMPReceiveIM(msg, nonce);
					FreeMessageObject(msg);
				} else if (strcmp(event->valuestring, "StatusUpdate") == 0) {
					printf("Status update event\n");
					cJSON* ustatus = cJSON_GetObjectItem(eventdata, "status");
					char* user =
						cJSON_GetObjectItem(eventdata, "user")->valuestring;
					status pstatus;
					pstatus.status =
						cJSON_GetObjectItem(ustatus, "status")->valuestring;
					pstatus.RPCDesc =
						cJSON_GetObjectItem(ustatus, "RPCDesc")->valuestring;
					pstatus.RPCIcon =
						cJSON_GetObjectItem(ustatus, "RPCIcon")->valuestring;
					pstatus.RPCName =
						cJSON_GetObjectItem(ustatus, "RPCName")->valuestring;
					onYAMPStatusUpdate(user, pstatus);
				} else if (strcmp(event->valuestring, "IncomingFriendReq") ==
						   0) {
					char* from =
						cJSON_GetObjectItem(eventdata, "from")->valuestring;
					onYAMPFriendRequestReceived(from);
				} else if (strcmp(event->valuestring, "UpdatedChannels") == 0) {
					cJSON* channelarr =
						cJSON_GetObjectItem(eventdata, "channels");
					YampChannel* charr = malloc(sizeof(YampChannel) *
												cJSON_GetArraySize(channelarr));
					for (int i = 0; i < cJSON_GetArraySize(channelarr); i++) {
						charr[i] = RecursiveParseChannel(
							cJSON_GetArrayItem(channelarr, i));
					}
					onYAMPChannelsUpdated(
						charr, cJSON_GetArraySize(channelarr),
						cJSON_GetObjectItem(eventdata, "space")->valuestring);
				} else if (strcmp(event->valuestring, "NewSpace") == 0) {
					YampSpace space = ParseSpaceObject(eventdata);
					onYAMPNewSpace(space);
				} else if (strcmp(event->valuestring, "NewFriend") == 0) {
					YampUser friend = ParseUserObject(eventdata);
					onYAMPNewFriend(friend);
				} else if (strcmp(event->valuestring, "Typing") == 0) {
					cJSON* cuser = cJSON_GetObjectItem(eventdata, "user");
					char* channel = cJSON_GetObjectItem(eventdata, "channel_id")
										->valuestring;
					YampUser user = ParseUserObject(cuser);
					onYAMPTyping(user, channel);
					YampUserFreeStrings(&user);
				} else if (strcmp(event->valuestring, "MessageDeleted") == 0) {
					onYAMPMessageDeleted(cJSON_GetObjectItem(eventdata, "message_id")
										->valuestring);
				} else if (strcmp(event->valuestring, "JoinChannel") == 0) {
					char* channel_id = cJSON_GetObjectItem(eventdata, "channel_id")
										->valuestring;
					YampUser usr = ParseUserObject(cJSON_GetObjectItem(eventdata, "user"));
					onYAMPChannelJoined(&usr,channel_id);
					YampUserFreeStrings(&usr);
				} else if (strcmp(event->valuestring, "LeftChannel") == 0) {
					char* channel_id = cJSON_GetObjectItem(eventdata, "channel_id")
										->valuestring;
					YampUser usr = ParseUserObject(cJSON_GetObjectItem(eventdata, "user"));
					onYAMPChannelLeave(&usr,channel_id);
					YampUserFreeStrings(&usr);
				}
			}
			cJSON_Delete(srvr);
			free(payload);
		} else {
			onYAMPDisconnected();
			return 0;
		}
	}
	return 0;
}
int YAMPConnect(const char* server, int* fd_out, SSL** socket_out) {
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char port_str[8];
	snprintf(port_str, sizeof(port_str), "%d", YAMP_PORT);

	int err = getaddrinfo(server, port_str, &hints, &res);
	if (err != 0) {
		return -1;
	}

	int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0) {
		freeaddrinfo(res);
		return -1;
	}

	if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
		freeaddrinfo(res);
#ifdef _WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		return -1;
	}

	freeaddrinfo(res);

	*fd_out = sock;
	SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
	SSL* sslsock = SSL_new(ctx);
	SSL_set_fd(sslsock, sock);
	int ret = SSL_connect(sslsock);
	if (ret != 1) {
		int sslerr = SSL_get_error(sslsock, ret);
		fprintf(stderr, "SSL_connect failed, SSL_get_error=%d\n", sslerr);
		SSL_free(sslsock);
		SSL_CTX_free(ctx);
#ifdef _WIN32
		closesocket(sock);
#else
		close(sock);
#endif
		return -1;
	}
	*socket_out = sslsock;
	pthread_t* recvthread = malloc(sizeof(pthread_t));
	pthread_create(recvthread, NULL, YAMPRecvLoop, sslsock);

	return 0;
}
static int YAMPSendRequest(SSL* fd, const char* reqid, const char* endpoint,
						   cJSON* data) {
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "reqid", reqid);
	cJSON_AddStringToObject(root, "type", "request");
	cJSON_AddStringToObject(root, "endpoint", endpoint);
	cJSON_AddItemToObject(root, "payload", data);
	char* finalPayload = cJSON_PrintUnformatted(root);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	cJSON_Delete(root);
	return 1;
}

int YAMPLogin(SSL* fd, char* username, char* password) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "username", username);
	cJSON_AddStringToObject(data, "password", password);
	YAMPSendRequest(fd, "0", "login", data);
	return 0;
}

int YAMPRegister(SSL* fd, char* username, char* password) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "username", username);
	cJSON_AddStringToObject(data, "password", password);
	YAMPSendRequest(fd, "register", "register", data);
	return 0;
}

int YAMPListBuddies(SSL* fd) {
	YAMPSendRequest(fd, "1", "ListFriends", cJSON_CreateObject());
	return 0;
}

int YAMPListConversations(SSL* fd) {
	YAMPSendRequest(fd, "convlist", "ListConversations", cJSON_CreateObject());
	return 0;
}
// the message type here is slimmed, only relevant fields are used, you dont have to fill most of the fields
int YAMPSendMessage(SSL* fd, char* where, YampMessage* msg, int nonce) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", where);
	cJSON_AddStringToObject(data, "content", msg->content);
	cJSON_AddStringToObject(data, "reply_id", msg->reply->id);
	cJSON_AddNumberToObject(data, "nonce", nonce);
	YAMPSendRequest(fd, where, "SendMessage", data);
	return 0;
}

int YAMPListSpaceChannels(SSL* fd, char* space) {
	char* reqid = g_strdup_printf("2%s", space);
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "space", space);
	YAMPSendRequest(fd, reqid, "GetChannels", data);
	g_free(reqid);
	return 1;
}

int YAMPGetMessageHistory(SSL* fd, char* channel, int limit, char* before) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", channel);
	cJSON_AddNumberToObject(data, "limit", limit);
	if(before){
		cJSON_AddStringToObject(data, "before", before);
	}
	YAMPSendRequest(fd, "GetMessageHistory", "GetMessageHistory", data);
	return 1;
}

int YAMPInsertSpace(SSL* fd, char* name, char* display_name, int type,
					char* description, char* banner, char* pfp) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "name", name);
	cJSON_AddStringToObject(data, "display_name", display_name);
	if (description) {
		cJSON_AddStringToObject(data, "description", description);
	}
	if (banner) {
		cJSON_AddStringToObject(data, "banner", banner);
	}
	if (pfp) {
		cJSON_AddStringToObject(data, "icon", pfp);
	}
	cJSON_AddNumberToObject(data, "space_type", type);
	YAMPSendRequest(fd, "CreateSpace", "CreateSpace", data);
	return 1;
}

int SplitAddress(char* address, char** username, char** server) {
	char* newAddr = strdup(address);
	char* at = strchr(newAddr, '@');
	if (!at) {
		free(newAddr);
		return 0; // get gud get @
	}
	*at = '\0';
	*username = newAddr;
	*server = at + 1;
	return 1;
}

int YAMPSendFriendReq(SSL* fd, char* to) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "to", to);
	YAMPSendRequest(fd, "SendFriendReq", "SendFriendReq", data);
	return 1;
}

int YAMPAcceptFriendReq(SSL* fd, char* userid) {
	char* reqid = g_strdup_printf("AcceptFriendReq|%s", userid);
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "user", userid);
	YAMPSendRequest(fd, reqid, "AcceptFriendReq", data);
	g_free(reqid);
	return 1;
}

int YAMPDenyFriendReq(SSL* fd, char* user) {
	char* reqid = g_strdup_printf("DenyFriendReq|%s", user);
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "user", user);
	YAMPSendRequest(fd, reqid, "DenyFriendReq", data);
	g_free(reqid);
	return 1;
}

int YAMPCreateChannel(SSL* fd, char* space, char* name, int pos, int type,
					  char* parent) {
	char* reqid = g_strdup_printf("InsertChannel|%s|%s", space, name);
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "space", space);
	cJSON_AddStringToObject(data, "name", name);
	if (pos >= 0) {
		cJSON_AddNumberToObject(data, "pos", pos);
	}
	cJSON_AddNumberToObject(data, "channeltype", type);
	if (parent) {
		cJSON_AddStringToObject(data, "parent", parent);
	} else {
		cJSON_AddNullToObject(data, "parent");
	}
	YAMPSendRequest(fd, reqid, "InsertChannel", data);
	g_free(reqid);
	return 1;
}

int YAMPUpdateUserProfile(SSL* fd, YampUser usr) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddItemToObject(data, "profile", CreateUserObject(usr));
	YAMPSendRequest(fd, "UpdateUserProfile", "UpdateUserProfile", data);
	return 1;
}
int YAMPStartDM(SSL* fd, char* targetusr) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "recipient", targetusr);
	YAMPSendRequest(fd, "StartDM", "StartDM", data);
	return 1;
}
int YAMPCreateGC(SSL* fd, char** init_members, int ninitmem) {
	cJSON* data = cJSON_CreateObject();
	cJSON* memarr = cJSON_CreateArray();
	for (int i = 0; i < ninitmem; i++) {
		cJSON_AddItemToArray(memarr, cJSON_CreateString(init_members[i]));
	}
	cJSON_AddItemToObject(data, "members", memarr);
	YAMPSendRequest(fd, "CreateGC", "CreateGC", data);
	return 1;
}
int YAMPJoinChannel(SSL* fd, char* channel) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", channel);
	YAMPSendRequest(fd, "JoinChannel", "JoinChannel", data);
	return 1;
}
int YAMPLeaveChannel(SSL* fd, char* channel) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", channel);
	YAMPSendRequest(fd, "LeaveChannel", "LeaveChannel", data);
	return 1;
}
int YAMPTyping(SSL* fd, char* channel) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", channel);
	YAMPSendRequest(fd, "Typing", "Typing", data);
	return 1;
}
char* YAMPGetHTTPBaseURI() { return YampHTTPAddress; }
int YAMPQueryYAMPHTTP() { return YampHTTPAddress ? 1 : 0; }
int YAMPDeleteMessage(SSL* fd, char* msg_id) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "message_id", msg_id);
	YAMPSendRequest(fd, "DeleteMessage", "DeleteMessage", data);
	return 1;
}