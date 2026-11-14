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
#include <curl/curl.h>
typedef SOCKET socket_fd;
#else
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <curl/curl.h>
#endif
#define YAMP_PORT 5225

typedef void (*YampFriendsListedCb)(YampUser** friends, int nfriends);
typedef void (*YampLoginResultCb)(char* token, int success);
typedef void (*YampRegisterResultCb)(char* token, int success);
typedef void (*YampAuthCb)(YampLoginData* payload);
typedef void (*YampChannelsFetchedCb)(YampChannel* channels, int n);
typedef void (*YampMessageHistoryCb)(YampMessage** msgs, int nmsgs,
									 int hasmore);
typedef void (*YampFriendReqSentCb)(int success);
typedef void (*YampFriendReqResolvedCb)(char* username, int success);
typedef void (*YampFileUploaded)(char* filename, int success, void* userdata);
typedef void (*YampInviteCreatedCb)(char* code, int success, void* userdata);

typedef struct {
	char* mime;
	unsigned char* file;
	int len;
	void* userdata;
} UploadFilePayload;

typedef struct CallbackStackEntry {
	void(*callback);
	int reqid;
	struct CallbackStackEntry* before;
	struct CallbackStackEntry* after;
	char* endpoint;
	void* data;
} CallbackStackEntry;


CallbackStackEntry* CallbackStack = NULL;
YampEventListener* EventListeners = NULL;



#define container_of(ptr, type, member)                                        \
	((type*)((char*)(ptr) - offsetof(type, member)))

int last_reqid = 0;

static char* dup0(const char* s) { return s ? strdup(s) : NULL; }
static void CopyID(char* dst, size_t cap, const char* src) {
	strncpy(dst, src ? src : "", cap);
}


void YampSpaceDupStrings(YampSpace* s) {
	s->name = dup0(s->name);
	s->displayname = dup0(s->displayname);
	s->description = dup0(s->description);
	s->banner = dup0(s->banner);
	s->icon = dup0(s->icon);
}
YampSpace YampSpaceDupStringless(const YampSpace* s) { return *s; }
YampSpace DupSpaceObject(const YampSpace* s) {
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
		r[i] = DupSpaceObject(&a[i]);
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

	sp.name = dup0(JsonStrOrNull(raw, "name"));
	sp.displayname = dup0(JsonStrOrNull(raw, "display_name"));
	sp.description = dup0(JsonStrOrNull(raw, "description"));
	sp.banner = dup0(JsonStrOrNull(raw, "banner"));
	sp.icon = dup0(JsonStrOrNull(raw, "icon"));

	cJSON* type = cJSON_GetObjectItem(raw, "type");
	if (!type)
		type = cJSON_GetObjectItem(raw, "space_type");
	sp.type = cJSON_IsNumber(type) ? type->valueint : 0;

	return sp;
}




cJSON* CreateStatusJSON(YampStatus* status) {
	cJSON* stat = cJSON_CreateObject();
	cJSON_AddStringToObject(stat, "status", status->status);
	cJSON_AddStringToObject(stat, "rpc_desc", status->RPCDesc);
	cJSON_AddStringToObject(stat, "rpc_icon", status->RPCIcon);
	cJSON_AddStringToObject(stat, "rpc_name", status->RPCName);
	return stat;
}
YampStatus* ParseStatusJSON(cJSON* status) {
	YampStatus* stat = malloc(sizeof(*stat));
	stat->status =
		dup0(cJSON_GetStringValue(cJSON_GetObjectItem(status, "status")));
	stat->RPCDesc =
		dup0(cJSON_GetStringValue(cJSON_GetObjectItem(status, "rpc_desc")));
	stat->RPCIcon =
		dup0(cJSON_GetStringValue(cJSON_GetObjectItem(status, "rpc_icon")));
	stat->RPCName =
		dup0(cJSON_GetStringValue(cJSON_GetObjectItem(status, "rpc_name")));
	return stat;
}
void FreeStatusFields(YampStatus* stat) {
	if (!stat) {
		return;
	}
	free(stat->status);
	free(stat->RPCDesc);
	free(stat->RPCIcon);
	free(stat->RPCName);
}
void FreeStatusObject(YampStatus* stat) {
	if (!stat) {
		return;
	}
	FreeStatusFields(stat);
	free(stat);
}
YampStatus* DupStatusObject(YampStatus* stat) {
	if (!stat) {
		return NULL;
	}
	YampStatus* heapstat = malloc(sizeof(*heapstat));
	heapstat->status = dup0(stat->status);
	heapstat->RPCDesc = dup0(stat->RPCDesc);
	heapstat->RPCIcon = dup0(stat->RPCIcon);
	heapstat->RPCName = dup0(stat->RPCName);
	return heapstat;
}
void FreeUserFields(YampUser* u) {
	free(u->username);
	free(u->displayname);
	free(u->pfp);
	free(u->description);
	FreeStatusObject(u->status);
}
void FreeUserObject(YampUser* u) {
	if (!u)
		return;
	FreeUserFields(u);
	free(u);
}

cJSON* CreateUserJSON(YampUser* user) {
	cJSON* obj = cJSON_CreateObject();

	cJSON_AddStringToObject(obj, "id", user->id);
	cJSON_AddStringToObject(obj, "name", user->username);
	cJSON_AddStringToObject(obj, "display_name", user->displayname);
	cJSON_AddStringToObject(obj, "description", user->description);
	cJSON_AddStringToObject(obj, "avatar", user->pfp);

	if(user->status){
		cJSON* status_obj = CreateStatusJSON(user->status);
		cJSON_AddItemToObject(obj, "status", status_obj);
	}

	return obj;
}

YampUser* DupUserObject(YampUser* usr) {
	YampUser* heapusr = malloc(sizeof(*heapusr));
	*heapusr = *usr;
	heapusr->username = dup0(usr->username);
	heapusr->displayname = dup0(usr->displayname);
	heapusr->description = dup0(usr->description);
	heapusr->pfp = dup0(usr->pfp);

	heapusr->status = DupStatusObject(usr->status);
	return heapusr;
}

YampUser* ParseUserJSON(cJSON* rawusr) {
	YampUser usr;
	strncpy(usr.id, cJSON_GetObjectItem(rawusr, "id")->valuestring, 17);
	usr.username = (cJSON_GetObjectItem(rawusr, "name")->valuestring);
	cJSON* rawdisp = cJSON_GetObjectItem(rawusr, "display_name");
	if (rawdisp) {
		usr.displayname = (rawdisp->valuestring);
	} else {
		usr.displayname = NULL;
	}
	cJSON* rawdesc = cJSON_GetObjectItem(rawusr, "description");
	if (rawdesc) {
		usr.description = (rawdesc->valuestring);
	} else {
		usr.description = NULL;
	}
	cJSON* rawpfp = cJSON_GetObjectItem(rawusr, "avatar");
	if (rawpfp) {
		usr.pfp = (rawpfp->valuestring);
	} else {
		usr.pfp = NULL;
	}
	cJSON* rawstatus = cJSON_GetObjectItem(rawusr, "status");
	usr.status = NULL;
	if (rawstatus) {
		usr.status = ParseStatusJSON(rawstatus);
	}

	return DupUserObject(&usr);
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
		ret.name = dup0(name->valuestring);
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
	ret.people = malloc(sizeof(YampUser*) * cJSON_GetArraySize(jpeople));
	int i = 0;
	const cJSON* item;
	cJSON_ArrayForEach(item, jpeople) { ret.people[i++] = ParseUserJSON(item); }

	return ret;
}

YampChannel DupChannelObject_Inline(const YampChannel* s) {
	YampChannel c = *s;

	c.name = dup0(s->name);

	if (s->children && s->nchildren > 0) {
		c.children = malloc(sizeof(YampChannel) * s->nchildren);
		for (int i = 0; i < s->nchildren; i++)
			c.children[i] = DupChannelObject_Inline(&s->children[i]);
	} else {
		c.children = NULL;
		c.nchildren = 0;
	}

	if (s->people && s->npeople > 0) {
		c.people = malloc(sizeof(YampUser*) * s->npeople);
		for (int i = 0; i < s->npeople; i++) {
			c.people[i] = DupUserObject(s->people[i]);
		}
	} else {
		c.people = NULL;
		c.npeople = 0;
	}

	return c;
}

YampChannel* DupChannelObject(const YampChannel* s) {
	YampChannel* c = malloc(sizeof(YampChannel));
	*c = DupChannelObject_Inline(s);
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

	for (int i = 0; i < ch->npeople; i++) {
		FreeUserObject(ch->people[i]);
	}
	free(ch->people);
	ch->people = NULL;
	ch->npeople = 0;
}
void FreeMessageObject(YampMessage* msg) {
	free(msg->content);
	if (msg->reply)
		FreeMessageObject(msg->reply);
	free(msg);
}
cJSON* CreateAttachmentJSON(YampAttachment* attachment) {
	cJSON* obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "url", attachment->url);
	cJSON_AddStringToObject(obj, "mime", attachment->mime);
	cJSON_AddStringToObject(obj, "filename", attachment->filename);
	return obj; // very meowsome
}
YampAttachment* AllocAttachmentType(YampAttachment* attachment) {
	YampAttachment* ret = malloc(sizeof(*ret));
	*ret = *attachment;
	ret->url = dup0(attachment->url);
	ret->mime = dup0(attachment->mime);
	ret->filename = dup0(attachment->filename);
	return ret;
}

YampAttachment* DupAttachmentObject(YampAttachment* attachment) {
	if (!attachment) {
		return NULL;
	}
	YampAttachment* ret = malloc(sizeof(*ret));
	*ret = *attachment;
	ret->url = dup0(attachment->url);
	ret->mime = dup0(attachment->mime);
	ret->filename = dup0(attachment->filename);
	return ret;
}

void FreeAttachmentTypeFields(YampAttachment* attach) {
	free(attach->url);
	free(attach->mime);
	free(attach->filename);
}
void FreeAttachmentObject(YampAttachment* attach) {
	FreeAttachmentTypeFields(attach);
	free(attach);
}
void FreeAttachmentObjectFieldless(YampAttachment* attach) { free(attach); }
YampAttachment* ParseAttachmentJSON(cJSON* json) {
	YampAttachment attachment = {0};
	attachment.mime = cJSON_GetStringValue(cJSON_GetObjectItem(json, "mime"));
	attachment.url = cJSON_GetStringValue(cJSON_GetObjectItem(json, "url"));
	attachment.filename =
		cJSON_GetStringValue(cJSON_GetObjectItem(json, "filename"));
	attachment.filename = attachment.filename
							  ? attachment.filename
							  : attachment.url; // Substitute it for the url
	return AllocAttachmentType(&attachment);
}
char* ProcessAttachmentURL(char* input) {
	char* url;
	char* enc;
	size_t len;
	if (strstr(input, "://")) {
		enc = g_uri_escape_string(input, NULL, FALSE);
		len = strlen(YampHTTPAddress) + strlen(enc) + 16;
		url = malloc(len);
		snprintf(url, len, "%s/cdn/proxy?url=%s", YampHTTPAddress, enc);
	} else {
		enc = g_uri_escape_string(input, NULL, FALSE);
		len = strlen(YampHTTPAddress) + strlen(enc) + 14;
		url = malloc(len);
		snprintf(url, len, "%s/cdn/content/%s", YampHTTPAddress, enc);
	}
	g_free(enc);
	return url;
}
YampMessage* ParseMessageObject(cJSON* object) {
	YampMessage* msg = malloc(sizeof(YampMessage));
	*msg = (YampMessage){0};
	msg->author = ParseUserJSON(cJSON_GetObjectItem(object, "author"));
	msg->content = strdup(cJSON_GetObjectItem(object, "content")->valuestring);
	strncpy(msg->channel, cJSON_GetObjectItem(object, "channel")->valuestring,
			34);
	if (cJSON_GetObjectItem(object, "reply")) {
		msg->reply = ParseMessageObject(cJSON_GetObjectItem(object, "reply"));
	}
	strncpy(msg->id, cJSON_GetObjectItem(object, "id")->valuestring, 17);
	msg->timestamp = cJSON_GetObjectItem(object, "timestamp")->valueint;
	msg->type = cJSON_GetObjectItem(object, "type")->valueint;
	cJSON* attarr = cJSON_GetObjectItem(object, "attachments");
	if (attarr) {
		for (int i = 0; i < cJSON_GetArraySize(attarr); i++) {
			YampAttachment* attach =
				ParseAttachmentJSON(cJSON_GetArrayItem(attarr, i));
			if (attach) {
				msg->attachments =
					realloc(msg->attachments,
							(msg->nAttachments + 1) * sizeof(YampAttachment*));
				msg->attachments[msg->nAttachments] = attach;
				msg->nAttachments++;
			}
		}
	}
	return msg;
}
YampMessage* DupMessageObject(YampMessage* msg) {
	YampMessage* heapmsg = malloc(sizeof(YampMessage));
	*heapmsg = *msg;
	heapmsg->content = strdup(msg->content);
	heapmsg->author = DupUserObject(heapmsg->author);
	if (msg->reply) {
		heapmsg->reply = DupMessageObject(msg->reply);
	}
	if (msg->attachments) {
		heapmsg->attachments =
			malloc(sizeof(YampAttachment*) * (heapmsg->nAttachments));
		for (int i = 0; i < heapmsg->nAttachments; i++) {
			heapmsg->attachments[i] = DupAttachmentObject(msg->attachments[i]);
		}
	}
	return heapmsg;
}

typedef struct {
	unsigned char* buf;
	int len;
} curl_buffer;
size_t curl_buffer_write(char* ptr, size_t size, size_t nmemb, void* userdata) {
	curl_buffer* buf = userdata;
	buf->buf = realloc(buf->buf, buf->len + nmemb);
	memcpy(buf->buf + buf->len, ptr, nmemb);
	buf->len += nmemb;
	return nmemb;
}
void HandleResponse(cJSON* srvr, CallbackStackEntry* callback_entry,
					int success) {
	cJSON* response = cJSON_GetObjectItem(srvr, "response");
	const char* ep = callback_entry->endpoint;
	void* cb = callback_entry->callback;
	void* dat = callback_entry->data;

	if (strcmp(ep, "ListFriends") == 0) {
		if (!cb)
			return;
		cJSON* rawfriends = cJSON_GetObjectItem(response, "friends");
		int n = cJSON_IsArray(rawfriends) ? cJSON_GetArraySize(rawfriends) : 0;
		YampUser** friends = malloc(sizeof(YampUser*) * (n ? n : 1));
		for (int i = 0; i < n; i++) {
			friends[i] = ParseUserJSON(cJSON_GetArrayItem(rawfriends, i));
		}
		((YampFriendsListedCb)cb)(friends, n);

	} else if (strcmp(ep, "login") == 0) {
		if (!cb)
			return;
		char* tok = NULL;
		if (success) {
			tok = cJSON_GetObjectItem(response, "token")->valuestring;
		}
		((YampLoginResultCb)cb)(tok, success);

	} else if (strcmp(ep, "register") == 0) {
		if (!cb)
			return;
		char* tok = NULL;
		if (success) {
			tok = cJSON_GetObjectItem(response, "token")->valuestring;
		}
		((YampRegisterResultCb)cb)(tok, success);

	} else if (strcmp(ep, "auth") == 0) {
		if (!success) {
			((YampAuthCb)cb)(NULL); // an error happened
			return;
		}
		cJSON* http = cJSON_GetObjectItem(response, "yamp-http");
		free(YampHTTPAddress);
		YampHTTPAddress =
			cJSON_IsString(http) ? strdup(http->valuestring) : NULL;
		if (!cb)
			return;

		cJSON* raw_incoming_fq = cJSON_GetObjectItem(response, "incoming_fq");
		cJSON* raw_outgoing_fq = cJSON_GetObjectItem(response, "outgoing_fq");
		cJSON* rawspaces = cJSON_GetObjectItem(response, "spaces");
		cJSON* rawconvs = cJSON_GetObjectItem(response, "conversations");
		cJSON* rawfriends = cJSON_GetObjectItem(response, "friends");

		int nincfq = cJSON_IsArray(raw_incoming_fq)
						 ? cJSON_GetArraySize(raw_incoming_fq)
						 : 0;
		int noutfq = cJSON_IsArray(raw_outgoing_fq)
						 ? cJSON_GetArraySize(raw_outgoing_fq)
						 : 0;
		int nspaces =
			cJSON_IsArray(rawspaces) ? cJSON_GetArraySize(rawspaces) : 0;
		int nconvs = cJSON_IsArray(rawconvs) ? cJSON_GetArraySize(rawconvs) : 0;
		int nfriends =
			cJSON_IsArray(rawfriends) ? cJSON_GetArraySize(rawfriends) : 0;

		YampUser** incoming_fq =
			malloc(sizeof(YampUser*) * (nincfq ? nincfq : 1));
		YampUser** outgoing_fq =
			malloc(sizeof(YampUser*) * (noutfq ? noutfq : 1));
		YampSpace* spaces = malloc(sizeof(YampSpace) * (nspaces ? nspaces : 1));
		YampChannel* convs =
			malloc(sizeof(YampChannel) * (nconvs ? nconvs : 1));
		YampUser** friends =
			malloc(sizeof(YampUser*) * (nfriends ? nfriends : 1));

		for (int i = 0; i < nincfq; i++)
			incoming_fq[i] =
				ParseUserJSON(cJSON_GetArrayItem(raw_incoming_fq, i));
		for (int i = 0; i < noutfq; i++)
			outgoing_fq[i] =
				ParseUserJSON(cJSON_GetArrayItem(raw_outgoing_fq, i));
		for (int i = 0; i < nspaces; i++)
			spaces[i] = ParseSpaceObject(cJSON_GetArrayItem(rawspaces, i));
		for (int i = 0; i < nconvs; i++)
			convs[i] = RecursiveParseChannel(cJSON_GetArrayItem(rawconvs, i));
		for (int i = 0; i < nfriends; i++)
			friends[i] = ParseUserJSON(cJSON_GetArrayItem(rawfriends, i));

		YampLoginData* heapdat = malloc(sizeof(YampLoginData));
		heapdat->usr = ParseUserJSON(cJSON_GetObjectItem(response, "user"));
		heapdat->conversations = convs;
		heapdat->nconversations = nconvs;
		heapdat->friends = friends;
		heapdat->nfriends = nfriends;
		heapdat->spaces = spaces;
		heapdat->nspaces = nspaces;
		heapdat->incfq = incoming_fq;
		heapdat->fqcount = nincfq;
		heapdat->outfq = outgoing_fq;
		heapdat->outfqcount = noutfq;
		((YampAuthCb)cb)(heapdat);

	} else if (strcmp(ep, "GetChannels") == 0) {
		if (!cb)
			return;
		cJSON* channelarr = cJSON_GetObjectItem(response, "channels");
		int n = cJSON_IsArray(channelarr) ? cJSON_GetArraySize(channelarr) : 0;
		YampChannel* charr = malloc(sizeof(YampChannel) * (n ? n : 1));
		for (int i = 0; i < n; i++) {
			charr[i] = RecursiveParseChannel(cJSON_GetArrayItem(channelarr, i));
		}
		((YampChannelsFetchedCb)cb)(charr, n);

	} else if (strcmp(ep, "GetMessageHistory") == 0) {
		if (!cb)
			return;
		cJSON* msgarr = cJSON_GetObjectItem(response, "messages");
		int n = cJSON_IsArray(msgarr) ? cJSON_GetArraySize(msgarr) : 0;
		YampMessage** msgs = malloc(sizeof(YampMessage*) * (n ? n : 1));
		for (int i = 0; i < n; i++) {
			msgs[i] = ParseMessageObject(cJSON_GetArrayItem(msgarr, i));
		}
		((YampMessageHistoryCb)cb)(
			msgs, n, cJSON_GetObjectItem(response, "has_more")->valueint);
		for (int i = 0; i < n; i++) {
			FreeMessageObject(msgs[i]);
		}
		free(msgs);

	} else if (strcmp(ep, "SendFriendReq") == 0) {
		if (cb)
			((YampFriendReqSentCb)cb)(success);

	} else if (strcmp(ep, "AcceptFriendReq") == 0 ||
			   strcmp(ep, "DenyFriendReq") == 0) {
		if (cb)
			((YampFriendReqResolvedCb)cb)(callback_entry->data, success);
		free(callback_entry->data);
	} else if (strcmp(ep, "UploadFile") == 0) {
		UploadFilePayload* payload = dat;
		YampFileUploaded done = (YampFileUploaded)cb;
		cJSON* tokitem = cJSON_GetObjectItem(response, "upload_token");

		if (!success || !cJSON_IsString(tokitem) || !YampHTTPAddress) {
			if (done)
				done(NULL, 0, payload->userdata);
			goto upload_cleanup;
		}

		size_t ulen =
			strlen(YampHTTPAddress) + strlen(tokitem->valuestring) + 32;
		char* url = malloc(ulen);
		snprintf(url, ulen, "%s/cdn/upload/%s", YampHTTPAddress,
				 tokitem->valuestring);

		size_t mlen = strlen(payload->mime) + 32;
		char* mime = malloc(mlen);
		snprintf(mime, mlen, "Content-Type: %s", payload->mime);

		struct curl_slist* headers = NULL;
		headers = curl_slist_append(headers, mime);
		headers = curl_slist_append(headers, "Expect:");

		curl_buffer respbuf = {0}; // zeroed
		CURL* curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload->file);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
						 (curl_off_t)payload->len);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_buffer_write);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &respbuf);

		char* filename_out = NULL;
		CURLcode rc = curl_easy_perform(curl);
		long http_code = 0;
		if (rc == CURLE_OK) {
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
			if ((http_code == 200 || http_code == 201) && respbuf.buf) {
				cJSON* resp =
					cJSON_ParseWithLength((char*)respbuf.buf, respbuf.len);
				cJSON* fn = resp ? cJSON_GetObjectItem(resp, "filename") : NULL;
				if (cJSON_IsString(fn))
					filename_out = strdup(fn->valuestring);
				cJSON_Delete(resp);
			} else {
				printf("file upload returned %i", http_code);
			}
		}
		if (done)
			done(filename_out, filename_out != NULL, payload->userdata);

		free(filename_out);
		free(respbuf.buf);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		free(mime);
		free(url);

	upload_cleanup:
		free(payload->mime);
		free(payload->file);
		free(payload);
	} else if (strcmp(ep, "JoinSpace") == 0) {
		// add a listener later
	} else if (strcmp(ep, "CreateSpaceInvite") == 0) {
		if (!cb)
			return;
		cJSON* code =
			success ? cJSON_GetObjectItem(response, "invite_code") : NULL;
		char* codestr = cJSON_IsString(code) ? code->valuestring : NULL;
		((YampInviteCreatedCb)cb)(codestr, codestr != NULL, dat);
	}
}
void YAMP_Init() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void YAMPRegisterEventListener(YampEventListener* listener) {
	if (!listener)
		return;

	for (YampEventListener* cur = EventListeners; cur; cur = cur->next) {
		if (cur == listener)
			return;
	}

	listener->next = NULL;

	if (!EventListeners) {
		EventListeners = listener;
		return;
	}

	YampEventListener* last = EventListeners;
	while (last->next)
		last = last->next;

	last->next = listener;
}
void* YAMPRecvLoop(void* fd) {
	uint32_t len;
	char* payload;
	while (1) {
		if (TLSYAMPRecv(fd, &payload, &len)) {
			cJSON* srvr = cJSON_Parse(payload);
			cJSON* type = cJSON_GetObjectItem(srvr, "type");
			int reqid =
				cJSON_GetNumberValue(cJSON_GetObjectItem(srvr, "reqid"));
			int success = cJSON_IsTrue(cJSON_GetObjectItem(srvr, "success"));
			if (strcmp(type->valuestring, "response") == 0) {
				if (CallbackStack) {
					for (CallbackStackEntry* callback = CallbackStack; callback;
						 callback = callback->after) {
						if (reqid == callback->reqid) {
							if (reqid == last_reqid) {
								last_reqid--;
							}
							HandleResponse(srvr, callback, success);
							if (callback->before) {
								callback->before->after = callback->after;
							} else {
								CallbackStack = callback->after;
							}
							if (callback->after)
								callback->after->before = callback->before;
							free(callback);
							break;
						}
					}
				}
			} else if (strcmp(type->valuestring, "event") == 0) {
				cJSON* event = cJSON_GetObjectItem(srvr, "event");
				cJSON* eventdata = cJSON_GetObjectItem(srvr, "data");

				if (!cJSON_IsString(event))
					goto event_done;

				const char* eventname = event->valuestring;

				if (strcmp(eventname, "recvim") == 0) {
					cJSON* cnonce = cJSON_GetObjectItem(eventdata, "nonce");
					int nonce = cJSON_IsNumber(cnonce)
									? cJSON_GetNumberValue(cnonce)
									: -1;

					YampMessage* msg = ParseMessageObject(
						cJSON_GetObjectItem(eventdata, "message"));

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onReceiveIM)
							l->onReceiveIM(msg, nonce);
					}

					FreeMessageObject(msg);

				} else if (strcmp(eventname, "StatusUpdate") == 0) {
					char* user = JsonStrOrNull(eventdata, "user");
					YampStatus* status = ParseStatusJSON(
						cJSON_GetObjectItem(eventdata, "status"));

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onStatusUpdate)
							l->onStatusUpdate(user, status);
					}

					FreeStatusObject(status);

				} else if (strcmp(eventname, "IncomingFriendReq") == 0) {
					char* from = JsonStrOrNull(eventdata, "from");

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onFriendRequestReceived)
							l->onFriendRequestReceived(from);
					}

				} else if (strcmp(eventname, "UpdatedChannels") == 0) {
					cJSON* channelarr =
						cJSON_GetObjectItem(eventdata, "channels");

					int n = cJSON_IsArray(channelarr)
								? cJSON_GetArraySize(channelarr)
								: 0;

					YampChannel* charr =
						malloc(sizeof(YampChannel) * (n ? n : 1));

					for (int i = 0; i < n; i++) {
						charr[i] = RecursiveParseChannel(
							cJSON_GetArrayItem(channelarr, i));
					}

					char* space = JsonStrOrNull(eventdata, "space");

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onChannelsUpdated)
							l->onChannelsUpdated(charr, n, space);
					}

					for (int i = 0; i < n; i++)
						YampChannelFree(&charr[i]);

					free(charr);

				} else if (strcmp(eventname, "NewSpace") == 0) {
					YampSpace space = ParseSpaceObject(eventdata);

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onNewSpace)
							l->onNewSpace(space);
					}

					YampSpaceFreeStrings(&space);

				} else if (strcmp(eventname, "NewFriend") == 0) {
					YampUser* user = ParseUserJSON(eventdata);

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onNewFriend)
							l->onNewFriend(user);
					}

					FreeUserObject(user);

				} else if (strcmp(eventname, "NewConversation") == 0) {
					YampChannel conversation = RecursiveParseChannel(eventdata);

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onNewConversation)
							l->onNewConversation(&conversation);
					}

					YampChannelFree(&conversation);

				} else if (strcmp(eventname, "Typing") == 0) {
					YampUser* user =
						ParseUserJSON(cJSON_GetObjectItem(eventdata, "user"));

					char* channel = JsonStrOrNull(eventdata, "channel_id");

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onTyping)
							l->onTyping(user, channel);
					}

					FreeUserObject(user);

				} else if (strcmp(eventname, "MessageDeleted") == 0) {
					char* message_id = JsonStrOrNull(eventdata, "message_id");

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onMessageDeleted)
							l->onMessageDeleted(message_id);
					}

				} else if (strcmp(eventname, "JoinChannel") == 0) {
					char* channel_id = JsonStrOrNull(eventdata, "channel_id");

					YampUser* user =
						ParseUserJSON(cJSON_GetObjectItem(eventdata, "user"));

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onChannelJoined)
							l->onChannelJoined(user, channel_id);
					}

					FreeUserObject(user);

				} else if (strcmp(eventname, "LeftChannel") == 0) {
					char* channel_id = JsonStrOrNull(eventdata, "channel_id");

					YampUser* user =
						ParseUserJSON(cJSON_GetObjectItem(eventdata, "user"));

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onChannelLeave)
							l->onChannelLeave(user, channel_id);
					}

					FreeUserObject(user);
				} else if(strcmp(eventname,"ProfileUpdate") == 0){
					YampUser* user =
						ParseUserJSON(eventdata);

					for (YampEventListener* l = EventListeners; l;
						 l = l->next) {
						if (l->onProfileUpdated)
							l->onProfileUpdated(user);
					}

					FreeUserObject(user);
				}

			event_done:;
			}
			cJSON_Delete(srvr);
			free(payload);
		} else {
			for (YampEventListener* l = EventListeners; l; l = l->next) {
				if (l->onDisconnected)
					l->onDisconnected();
			}
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

void YAMPPushCallback(int reqid, char* endpoint, void* callback, void* data) {
	CallbackStackEntry* entry = malloc(sizeof(*entry));
	if (!entry)
		return;

	entry->callback = callback;
	entry->reqid = reqid;
	entry->endpoint = endpoint;
	entry->data = data;
	entry->after = NULL;

	CallbackStackEntry* last_entry = CallbackStack;

	while (last_entry && last_entry->after)
		last_entry = last_entry->after;

	entry->before = last_entry;

	if (last_entry)
		last_entry->after = entry;
	else
		CallbackStack = entry;
}
int YAMPSendRequest(SSL* fd, const char* endpoint, cJSON* data, void* callback,
					void* userdata) {
	int reqid = last_reqid++;
	YAMPPushCallback(reqid, endpoint, callback, userdata);

	cJSON* root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "reqid", reqid);
	cJSON_AddStringToObject(root, "type", "request");
	cJSON_AddStringToObject(root, "endpoint", endpoint);
	cJSON_AddItemToObject(root, "payload", data);
	char* finalPayload = cJSON_PrintUnformatted(root);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	cJSON_Delete(root);
	return 1;
}
int YAMPAuth(SSL* fd, char* token, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "token", token);
	YAMPSendRequest(fd, "auth", data, callback, NULL);
	return 0;
}
int YAMPLogin(SSL* fd, char* email, char* password, char* device,
			  int devicetype, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "email", email);
	cJSON_AddStringToObject(data, "password", password);
	cJSON_AddStringToObject(data, "device", device);
	cJSON_AddNumberToObject(data, "device_type", devicetype);
	YAMPSendRequest(fd, "login", data, callback, NULL);
	return 0;
}
int YAMPRegister(SSL* fd, char* email, char* username, char* password,
				 char* device, int devicetype, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "username", username);
	cJSON_AddStringToObject(data, "email", email);
	cJSON_AddStringToObject(data, "password", password);
	cJSON_AddStringToObject(data, "device", device);
	cJSON_AddNumberToObject(data, "device_type", devicetype);
	YAMPSendRequest(fd, "register", data, callback, NULL);
	return 0;
}

int YAMPListBuddies(SSL* fd, void* callback) {
	YAMPSendRequest(fd, "ListFriends", cJSON_CreateObject(), callback, NULL);
	return 0;
}

int YAMPListConversations(SSL* fd, void* callback) {
	YAMPSendRequest(fd, "ListConversations", cJSON_CreateObject(), callback,
					NULL);
	return 0;
}
// the message type here is slimmed, only relevant fields are used, you dont
// have to fill most of the fields
int YAMPSendMessage(SSL* fd, char* where, YampMessage* msg, int nonce,
					void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", where);
	cJSON_AddStringToObject(data, "content", msg->content);
	cJSON_AddStringToObject(data, "reply_id", msg->reply->id);
	cJSON* attachments = msg->nAttachments ? cJSON_CreateArray() : NULL;
	for (int i = 0; i < msg->nAttachments; i++) {
		cJSON* attachment = CreateAttachmentJSON(msg->attachments[i]);
		cJSON_AddItemToArray(attachments, attachment);
	}
	cJSON_AddItemToObject(data, "attachments", attachments);
	cJSON_AddNumberToObject(data, "nonce", nonce);
	YAMPSendRequest(fd, "SendMessage", data, callback, NULL);
	return 0;
}

int YAMPListSpaceChannels(SSL* fd, char* space, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "space", space);
	YAMPSendRequest(fd, "GetChannels", data, callback, NULL);
	return 1;
}

int YAMPGetMessageHistory(SSL* fd, char* channel, int limit, char* before,
						  void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", channel);
	cJSON_AddNumberToObject(data, "limit", limit);
	if (before) {
		cJSON_AddStringToObject(data, "before", before);
	}
	YAMPSendRequest(fd, "GetMessageHistory", data, callback, NULL);
	return 1;
}

int YAMPInsertSpace(SSL* fd, char* name, char* display_name, int type,
					char* description, char* banner, char* pfp,
					void* callback) {
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
	YAMPSendRequest(fd, "CreateSpace", data, callback, NULL);
	return 1;
}

int YAMPSendFriendReq(SSL* fd, char* to, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "to", to);
	YAMPSendRequest(fd, "SendFriendReq", data, callback, NULL);
	return 1;
}

int YAMPAcceptFriendReq(SSL* fd, char* userid, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "user", userid);
	YAMPSendRequest(fd, "AcceptFriendReq", data, callback, strdup(userid));
	return 1;
}

int YAMPDenyFriendReq(SSL* fd, char* user, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "user", user);
	YAMPSendRequest(fd, "DenyFriendReq", data, callback, strdup(user));
	return 1;
}

int YAMPCreateChannel(SSL* fd, char* space, char* name, int pos, int type,
					  char* parent, void* callback) {
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
	YAMPSendRequest(fd, "InsertChannel", data, callback, NULL);
	return 1;
}

int YAMPUpdateUserProfile(SSL* fd, YampUser* usr, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddItemToObject(data, "profile", CreateUserJSON(usr));
	YAMPSendRequest(fd, "UpdateUserProfile", data, callback, NULL);
	return 1;
}
int YAMPStartDM(SSL* fd, char* targetusr, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "recipient", targetusr);
	YAMPSendRequest(fd, "StartDM", data, callback, NULL);
	return 1;
}
int YAMPCreateGC(SSL* fd, char** init_members, int ninitmem, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON* memarr = cJSON_CreateArray();
	for (int i = 0; i < ninitmem; i++) {
		cJSON_AddItemToArray(memarr, cJSON_CreateString(init_members[i]));
	}
	cJSON_AddItemToObject(data, "members", memarr);
	YAMPSendRequest(fd, "CreateGC", data, callback, NULL);
	return 1;
}
int YAMPJoinChannel(SSL* fd, char* channel, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", channel);
	YAMPSendRequest(fd, "JoinChannel", data, callback, NULL);
	return 1;
}
int YAMPLeaveChannel(SSL* fd, char* channel, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", channel);
	YAMPSendRequest(fd, "LeaveChannel", data, callback, NULL);
	return 1;
}
int YAMPTyping(SSL* fd, char* channel, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "channel", channel);
	YAMPSendRequest(fd, "Typing", data, callback, NULL);
	return 1;
}
int YAMPDeleteMessage(SSL* fd, char* msg_id, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "message_id", msg_id);
	YAMPSendRequest(fd, "DeleteMessage", data, callback, NULL);
	return 1;
}
int YAMPJoinSpace(SSL* fd, char* code, void* callback) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "code", code);
	YAMPSendRequest(fd, "JoinSpace", data, callback, NULL);
	return 1;
}
int YAMPCreateSpaceInvite(SSL* fd, char* space, int uses, void* callback,
						  void* userdata) {
	cJSON* data = cJSON_CreateObject();
	cJSON_AddStringToObject(data, "space", space);
	cJSON_AddNumberToObject(data, "uses", uses);
	YAMPSendRequest(fd, "CreateSpaceInvite", data, callback, userdata);
	return 1;
}
char* YAMPGetHTTPBaseURI() { return YampHTTPAddress; }
int YAMPQueryYAMPHTTP() { return YampHTTPAddress ? 1 : 0; }
int YAMPUploadFile(SSL* fd, char* MIME, unsigned char* file, int len,
				   void* callback, void* userdata) {
	UploadFilePayload* payload = malloc(sizeof(*payload));
	payload->mime = strdup(MIME);
	payload->file = malloc(len);
	memcpy(payload->file, file, len);
	payload->len = len;
	payload->userdata = userdata;

	cJSON* data = cJSON_CreateObject();
	YAMPSendRequest(fd, "UploadFile", data, callback, payload);
	return 1;
}