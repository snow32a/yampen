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
extern void onYAMPBuddyListed(cJSON *Buddies);
extern void onYAMPUserDetailsFetched(cJSON *Detail);
extern void onYAMPSpacesFetched(cJSON *Spaces);
extern void onYAMPChannelsFetched(cJSON *Channels);
extern void onYAMPLoggedIn();
extern void onYAMPLoginFail();
extern void onYAMPDisconnected();
extern void onYAMPStatusUpdate(char *name, status stat);
char *MakeDMChannel(const char *a, const char *b) {
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
gboolean YAMPProcessWhere(char *where, char *curUsername, chat *out) {
	char *dupedwhere = strdup(where);
	char *safewhere = strdup(where);
	chat retval = {0};
	if (*where == '^') {
		// GUILD PROBABLY
		char *hashtag = strchr(safewhere, '#');
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
		char *minus = strchr(safewhere, '|');
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
extern void onYAMPReceiveIM(char *username, char *where, char *data);
int YAMPSend(socket_fd fd, void *payload, uint32_t size) {
	uint32_t NlSize = htonl(size);
	send(fd, &NlSize, 4, 0);
	send(fd, payload, size, 0);
}
int YAMPRecv(int fd, char **payload, uint32_t *len) {
	if (recv(fd, len, 4, 0) > 0) {
		*len = ntohl(*len);
		*payload = malloc(*len + 1);
		int totalread = 0;
		while (totalread < *len) {
			int r = recv(fd, *payload + totalread, *len, 0);
			totalread += r;
		}
		(*payload)[*len] = '\0';
		return 1;
	}
	return 0; // server got busted by a segfault :sob:
}
int TLSYAMPSend(SSL *fd, void *payload, uint32_t size) {
	uint32_t NlSize = htonl(size);
	SSL_write(fd, &NlSize, 4);
	return SSL_write(fd, payload, size);
}
int TLSYAMPRecv(SSL *fd, char **payload, uint32_t *len) {
	if (SSL_read(fd, len, 4) > 0) {
		*len = ntohl(*len);
		*payload = malloc(*len + 1);
		int totalread = 0;
		while (totalread < *len) {
			int r = SSL_read(fd, *payload + totalread, *len);
			totalread += r;
		}
		(*payload)[*len] = '\0';
		return 1;
	}
	return 0;
}
void *YAMPRecvLoop(void *fd) {

	uint32_t len;
	char *payload;
	while (1) {
		if (TLSYAMPRecv(fd, &payload, &len)) {
			cJSON *srvr = cJSON_Parse(payload);
			cJSON *type = cJSON_GetObjectItem(srvr, "type");
			if (strcmp(type->valuestring, "response") == 0) {
				cJSON *reqid = cJSON_GetObjectItem(srvr, "reqid");
				cJSON *response = cJSON_GetObjectItem(srvr, "response");
				if (strcmp(reqid->valuestring, "1") == 0) {
					printf("BUDDY LISTED\n");
					onYAMPBuddyListed(response);
				} else if (strcmp(reqid->valuestring, "0") == 0) {
					printf("LOGIN RESP\n");
					if (strcmp(response->valuestring, "success") == 0) {
						onYAMPUserDetailsFetched(
							cJSON_GetObjectItem(srvr, "user"));
						onYAMPLoggedIn();
						onYAMPSpacesFetched(cJSON_GetObjectItem(
							cJSON_GetObjectItem(srvr, "user"), "spaces"));
					} else {
						onYAMPLoginFail();
					}
				} else if (*(reqid->valuestring) == '2') {
					onYAMPChannelsFetched(
						cJSON_GetObjectItem(srvr, "response"));
				} else if (strcmp(reqid->valuestring, "GetMessageHistory") ==
						   0) {
					for (int i = 0; i < cJSON_GetArraySize(response); i++) {
						cJSON *msg = cJSON_GetArrayItem(response, i);
						onYAMPReceiveIM(
							cJSON_GetObjectItem(msg, "author")->valuestring,
							cJSON_GetObjectItem(msg, "where")->valuestring,
							cJSON_GetObjectItem(msg, "content")->valuestring);
					}
				}
			} else if (strcmp(type->valuestring, "event") == 0) {
				cJSON *event = cJSON_GetObjectItem(srvr, "event");
				cJSON *eventdata = cJSON_GetObjectItem(srvr, "data");
				if (strcmp(event->valuestring, "recvim") == 0) {
					char *content =
						cJSON_GetObjectItem(eventdata, "content")->valuestring;
					char *author =
						cJSON_GetObjectItem(eventdata, "author")->valuestring;
					char *where =
						cJSON_GetObjectItem(eventdata, "where")->valuestring;
					onYAMPReceiveIM(author, where, content);
				} else if (strcmp(event->valuestring, "StatusUpdate") == 0) {
					cJSON *ustatus = cJSON_GetObjectItem(eventdata, "status");
					char *user =
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
int YAMPConnect(const char *server, int *fd_out, SSL **socket_out) {
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
	SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
	SSL *sslsock = SSL_new(ctx);
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
	pthread_t *recvthread = malloc(sizeof(pthread_t));
	pthread_create(recvthread, NULL, YAMPRecvLoop, sslsock);

	return 0;
}
int YAMPLogin(SSL *fd, char *username, char *password) {
	cJSON *payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "username", username);
	cJSON_AddStringToObject(payload, "password", password);
	cJSON_AddStringToObject(payload, "reqid", "0");
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "login");
	char *finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	return 0;
}
int YAMPListBuddies(SSL *fd) {
	cJSON *payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "reqid", "1");
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "buddylist");
	char *finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	return 0;
}
int YAMPSendIM(SSL *fd, char *where, char *content) {
	cJSON *payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "reqid", where);
	cJSON_AddStringToObject(payload, "where", where);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "sendim");
	cJSON_AddStringToObject(payload, "content", content);
	char *finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	return 0;
}
int YAMPListSpaceChannels(SSL *fd, char *space) {
	cJSON *payload = cJSON_CreateObject();
	char *reqid = malloc(strlen(space) + 1 + 1);
	sprintf(reqid, "2%s", space);
	cJSON_AddStringToObject(payload, "reqid", reqid);
	cJSON_AddStringToObject(payload, "space", space);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "getchannels");
	char *finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	free(reqid);
	return 0;
}
int YAMPGetMessageHistory(SSL *fd, char *where) {
	// printf("trigger\n");
	cJSON *payload = cJSON_CreateObject();
	cJSON_AddStringToObject(payload, "reqid", "GetMessageHistory");
	cJSON_AddStringToObject(payload, "where", where);
	cJSON_AddStringToObject(payload, "type", "request");
	cJSON_AddStringToObject(payload, "endpoint", "GetMessageHistory");
	char *finalPayload = cJSON_Print(payload);
	TLSYAMPSend(fd, finalPayload, strlen(finalPayload));
	cJSON_free(finalPayload);
	return 0;
}
int SplitAddress(char *address, char **username, char **server) {
	char *newAddr = strdup(address);
	char *at = strchr(newAddr, '@');
	if (!at) {
		return 0; // get gud get @
	}
	*at = '\0';
	*username = newAddr;
	*server = at + 1;
	return 1;
}
