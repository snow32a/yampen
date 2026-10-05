#include "cjson/cJSON.h"
#include "glib.h"
#include "glibconfig.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/ssl.h>
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
extern void onYAMPUserDetailsFetched(cJSON* Detail);
extern void onYAMPSpacesFetched(YampSpace* spaces, int nspaces);
extern void onYAMPNewSpace(YampSpace spaces);
extern void onYAMPNewFriend(YampUser friend);
extern void onYAMPChannelsFetched(YampChannel* channels, int n);
extern void onYAMPChannelsUpdated(YampChannel* channels, int n, char* space);
extern void onYAMPLoggedIn(YampUser usr, YampSpace* spaces, int nspaces,
						   YampUser* incfq, int fqcount, YampUser* outfq,
						   int outfqcount);
extern void onYAMPLoginFail();
extern void onYAMPDisconnected();
extern void onYAMPStatusUpdate(char* name, status stat);
extern void onYAMPFriendRequestReceived(char* username);
extern void onYAMPFriendReqSent(int success);
extern void onYAMPRegisterResult(int success);
extern void onYAMPFriendReqResolved(char* username, int success);
char* MakeDMChannel(const char* a, const char* b) {
	if (strcmp(a, b) < 0)
		return g_strdup_printf("%s|%s", a, b);
	else
		return g_strdup_printf("%s|%s", b, a);
}

//////////////////////////////////////////
///    YAMP'S WHERE PARAMETER STYLE    ///
///  GUILDS:              DMS:         ///
/// ^guildName#channel    aguy-boi     ///
//////////////////////////////////////////
gboolean YAMPProcessWhere(char* where, char* curUsername, chat* out) {
	char* dupedwhere = strdup(where);
	char* safewhere = strdup(where);
	chat retval = {0};
	if (*where == '^') {
		// GUILD PROBABLY
		char* hashtag = strchr(safewhere, '#');
		if (!hashtag) {
			return FALSE;
		}
		*hashtag = '\0';
		retval.GuildName = safewhere + 1;
		retval.ChannelName = hashtag + 1;
		retval.OtherGuy = NULL;
		retval.type = YAMP_GUILD;
		retval.where = dupedwhere;
		*out = retval;
		return TRUE;
	} else {
		// Could be a damn DM?
		char* minus = strchr(safewhere, '|');
		if (!minus) {
			return FALSE; // nah it wasnt anything LMFAO
		}
		*minus = '\0';
		retval.ChannelName = NULL;
		retval.type = YAMP_DM;
		retval.where = dupedwhere;
		if (strcmp(minus + 1, curUsername) == 0) {
			retval.OtherGuy = safewhere;
		} else {
			retval.OtherGuy = minus + 1;
		}
		retval.GuildName = NULL;
		*out = retval;
		return TRUE;
	}
}
extern void onYAMPReceiveIM(char* username, char* where, char* data);
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
			int r = SSL_read(fd, *payload + totalread, *len-totalread);
			totalread += r;
		}
		(*payload)[*len] = '\0';
		return 1;
	}
	return 0;
}
YampChannel RecursiveParseChannel(cJSON* raw) {
	YampChannel ret;
	ret.pos = cJSON_GetObjectItem(raw, "position")->valueint;
	ret.type = cJSON_GetObjectItem(raw, "type")->valueint;
	ret.name = cJSON_GetObjectItem(raw, "name")->valuestring;
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
	return ret;
}
static char* JsonStrOrNull(cJSON* obj, const char* key) {
	cJSON* item = cJSON_GetObjectItem(obj, key);
	return cJSON_IsString(item) ? item->valuestring : NULL;
}

YampSpace ParseSpaceObject(cJSON* raw) {
	YampSpace sp = {0};

	char* id = JsonStrOrNull(raw, "id");
	if (id)
		strcpy(sp.id, id);

	sp.name = JsonStrOrNull(raw, "name");
	sp.displayname = JsonStrOrNull(raw, "display_name");
	sp.description = JsonStrOrNull(raw, "description");
	sp.banner = JsonStrOrNull(raw, "banner");
	sp.icon = JsonStrOrNull(raw, "icon");

	cJSON* type = cJSON_GetObjectItem(raw, "type");
	if (!type)
		type = cJSON_GetObjectItem(raw, "space_type");
	sp.type = cJSON_IsNumber(type) ? type->valueint : 0;

	return sp;
}
YampUser ParseUserObject(cJSON* rawusr) {
	YampUser usr;
	strncpy(usr.id, cJSON_GetObjectItem(rawusr, "id")->valuestring, 17);
	usr.username = cJSON_GetObjectItem(rawusr, "name")->valuestring;
	cJSON* rawdisp = cJSON_GetObjectItem(rawusr, "display_name");
	if (rawdisp) {
		usr.displayname = rawdisp->valuestring;
	} else {
		usr.displayname = NULL;
	}
	cJSON* rawdesc = cJSON_GetObjectItem(rawusr, "description");
	if (rawdesc) {
		usr.description = rawdesc->valuestring;
	} else {
		usr.description = NULL;
	}
	cJSON* rawpfp = cJSON_GetObjectItem(rawusr, "pfp");
	if (rawpfp) {
		usr.pfp = rawpfp->valuestring;
	} else {
		usr.pfp = NULL;
	}
	cJSON* rawstatus = cJSON_GetObjectItem(rawusr, "status");
	usr.status.status = cJSON_GetObjectItem(rawstatus, "status")->valuestring;
	usr.status.RPCName = cJSON_GetObjectItem(rawstatus, "RPCName")->valuestring;
	usr.status.RPCDesc = cJSON_GetObjectItem(rawstatus, "RPCDesc")->valuestring;
	usr.status.RPCIcon = cJSON_GetObjectItem(rawstatus, "RPCIcon")->valuestring;
	return usr;
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
					YampUser* friends = malloc(cJSON_GetArraySize(response)*sizeof(YampUser));
					for(int i = 0; i<cJSON_GetArraySize(response);i++){
						friends[i]=ParseUserObject(cJSON_GetArrayItem(response, i));
					}
					onYAMPFriendsListed(friends,cJSON_GetArraySize(response));
				} else if (strcmp(reqid->valuestring, "Register") == 0) {
					printf("REGISTER RESP\n");
					onYAMPRegisterResult(success);
				} else if (strcmp(reqid->valuestring, "0") == 0) {
					printf("LOGIN RESP\n");
					if (success) {
						cJSON* jsonusr = cJSON_GetObjectItem(srvr, "user");
						cJSON* raw_incoming_fq =
							cJSON_GetObjectItem(srvr, "incoming_fq");
						cJSON* raw_outgoing_fq =
							cJSON_GetObjectItem(srvr, "outgoing_fq");
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
						cJSON* rawspaces = cJSON_GetObjectItem(srvr, "spaces");
						int nspaces = cJSON_IsArray(rawspaces)
										  ? cJSON_GetArraySize(rawspaces)
										  : 0;
						YampSpace* spaces =
							malloc(sizeof(YampSpace) * (nspaces ? nspaces : 1));
						for (int i = 0; i < nspaces; i++) {
							spaces[i] = ParseSpaceObject(
								cJSON_GetArrayItem(rawspaces, i));
						}
						onYAMPLoggedIn(usr, spaces, nspaces, incoming_fq,
									   cJSON_GetArraySize(raw_incoming_fq),
									   outgoing_fq,
									   cJSON_GetArraySize(raw_outgoing_fq));
					} else {
						onYAMPLoginFail();
					}
				} else if (*(reqid->valuestring) == '2') {
					cJSON* channelarr = cJSON_GetObjectItem(srvr, "response");
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
					for (int i = 0; i < cJSON_GetArraySize(response); i++) {
						cJSON* msg = cJSON_GetArrayItem(response, i);
						onYAMPReceiveIM(
							cJSON_GetObjectItem(msg, "author")->valuestring,
							cJSON_GetObjectItem(msg, "where")->valuestring,
							cJSON_GetObjectItem(msg, "content")->valuestring);
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
					char* content =
						cJSON_GetObjectItem(eventdata, "content")->valuestring;
					char* author =
						cJSON_GetObjectItem(eventdata, "author")->valuestring;
					char* where =
						cJSON_GetObjectItem(eventdata, "where")->valuestring;
					onYAMPReceiveIM(author, where, content);
				} else if (strcmp(event->valuestring, "StatusUpdate") == 0) {
					cJSON* ustatus = cJSON_GetObjectItem(eventdata, "status");
					char* user =
						cJSON_GetObjectItem(eventdata, "name")->valuestring;
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
				}
			}
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
int YAMPLogin(SSL* fd, char* username, char* password) {
	cJSON* payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "username", username);
	cJSON_AddStringToObject(payload, "password", password);
	cJSON_AddStringToObject(payload, "reqid", "0");
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "login");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	return 0;
}
int YAMPRegister(SSL* fd, char* username, char* password) {
	cJSON* payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "username", username);
	cJSON_AddStringToObject(payload, "password", password);
	cJSON_AddStringToObject(payload, "reqid", "register");
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "register");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	return 0;
}
int YAMPListBuddies(SSL* fd) {
	cJSON* payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "reqid", "1");
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "ListFriends");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	return 0;
}
int YAMPSendIM(SSL* fd, char* where, char* content) {
	cJSON* payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "reqid", where);
	cJSON_AddStringToObject(payload, "where", where);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "SendMessage");
	cJSON_AddStringToObject(payload, "content", content);
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	return 0;
}
int YAMPListSpaceChannels(SSL* fd, char* space) {
	cJSON* payload = cJSON_CreateObject();
	char* reqid = malloc(strlen(space) + 1 + 1);
	sprintf(reqid, "2%s", space);
	cJSON_AddStringToObject(payload, "reqid", reqid);
	cJSON_AddStringToObject(payload, "space", space);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "getchannels");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	free(reqid);
	return 1;
}
int YAMPGetMessageHistory(SSL* fd, char* where) {
	// printf("trigger\n");
	cJSON* payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "reqid", "GetMessageHistory");
	cJSON_AddStringToObject(payload, "where", where);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "GetMessageHistory");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	return 1;
}
int YAMPInsertSpace(SSL* fd, char* name, char* display_name, int type,
					char* description, char* banner, char* pfp) {
	cJSON* payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "reqid", "CreateSpace");
	cJSON_AddStringToObject(payload, "name", name);
	cJSON_AddStringToObject(payload, "display_name", display_name);
	if (description) {
		cJSON_AddStringToObject(payload, "description", description);
	}
	if (banner) {
		cJSON_AddStringToObject(payload, "banner", banner);
	}
	if (pfp) {
		cJSON_AddStringToObject(payload, "icon", pfp);
	}
	cJSON_AddNumberToObject(payload, "space_type", type);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "CreateSpace");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	return 1;
}
int SplitAddress(char* address, char** username, char** server) {
	char* newAddr = strdup(address);
	char* at = strchr(newAddr, '@');
	if (!at) {
		return 0; // get gud get @
	}
	*at = '\0';
	*username = newAddr;
	*server = at + 1;
	return 1;
}



int YAMPSendFriendReq(SSL* fd, char* to) {
	cJSON* payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "reqid", "SendFriendReq");
	cJSON_AddStringToObject(payload, "to", to);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "SendFriendReq");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	cJSON_Delete(payload);
	return 1;
}

int YAMPAcceptFriendReq(SSL* fd, char* userid) {
	cJSON* payload = cJSON_CreateObject();
	// embed the username in reqid so the response handler knows who was
	// accepted, since the server doesn't currently echo it back otherwise
	char* reqid = g_strdup_printf("AcceptFriendReq|%s", userid);
	cJSON_AddStringToObject(payload, "reqid", reqid);
	cJSON_AddStringToObject(payload, "user", userid);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "AcceptFriendReq");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	cJSON_Delete(payload);
	g_free(reqid);
	return 1;
}

int YAMPDenyFriendReq(SSL* fd, char* user) {
	cJSON* payload = cJSON_CreateObject();
	char* reqid = g_strdup_printf("DenyFriendReq|%s", user);
	cJSON_AddStringToObject(payload, "reqid", reqid);
	cJSON_AddStringToObject(payload, "user", user);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "DenyFriendReq");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	cJSON_Delete(payload);
	g_free(reqid);
	return 1;
}

int YAMPCreateChannel(SSL* fd, char* space, char* name, int pos, int type,
					  char* parent) {
	cJSON* payload = cJSON_CreateObject();
	char* reqid = g_strdup_printf("InsertChannel|%s|%s", space, name);
	cJSON_AddStringToObject(payload, "reqid", reqid);
	cJSON_AddStringToObject(payload, "space", space);
	cJSON_AddStringToObject(payload, "name", name);
	if (pos >= 0) {
		cJSON_AddNumberToObject(payload, "pos", pos);
	}
	cJSON_AddNumberToObject(payload, "channeltype", type);
	if (parent) {
		cJSON_AddStringToObject(payload, "parent", parent);
	} else {
		cJSON_AddNullToObject(payload, "parent");
	}
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "InsertChannel");
	char* finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	cJSON_Delete(payload);
	g_free(reqid);
	return 1;
}