#include <glib.h>
#include "hashtables.h"
#include <gtk/gtk.h>
#include <yamp.h>
#include "types.h"
GHashTable* dtt;
GHashTable* ctt;
GHashTable* stt;
GHashTable* chatwndmap;
GHashTable* space_channel_cache;
GHashTable* pfpmap;
GHashTable* MessageSilhouettes;
GHashTable* space_channel_cache;
GHashTable* channel_msg_cache;
static void FreeSpaceObj(gpointer data) {
	YampSpace* t = data;
	// g_free(t->name);
	g_free(t);
}
static void FreeUserObj(gpointer data) {
	YampSpace* t = data;
	// g_free(t->name);
	g_free(t);
}
void InitUserIDTable() {
	dtt = g_hash_table_new(g_str_hash, g_str_equal);
}
void InitConversationsTable() {
	ctt = g_hash_table_new(g_str_hash, g_str_equal);
}
void InitSpaceIDTable() {
	stt = g_hash_table_new(g_str_hash, g_str_equal);
}
void InitChatWindowTable() {
	chatwndmap = g_hash_table_new(g_str_hash, g_str_equal);
}
static void FreeSpaceChannelCache(gpointer data) {
    SpaceChannelCache *c = data;
    if (!c) return;
    FreeChannelArray(c->channels,c->count);
    g_free(c);
}

void InitSpaceChannelsCache(void) {
    space_channel_cache = g_hash_table_new_full(
        g_str_hash, g_str_equal,
        g_free,
        FreeSpaceChannelCache
    );
}
static void FreeMessageCache(gpointer data) {
    ChMsgCache *c = data;
    if (!c) return;
    FreeMessageArray(c->messages,c->count);
    g_free(c);
}
void InitMessageCache(void) {
    channel_msg_cache = g_hash_table_new_full(
        g_str_hash, g_str_equal,
        g_free,
        FreeMessageCache
    );
}

void InitMessageSilhouettes(void) {
    MessageSilhouettes = g_hash_table_new_full(
        g_direct_hash,
        g_direct_equal,
        NULL,
        free
    );
}

void InitPfpTable() {
	pfpmap = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}
void InitAllTables() {
	InitUserIDTable();
	InitConversationsTable();
	InitSpaceIDTable();
	InitSpaceChannelsCache();
	InitMessageCache();
	InitChatWindowTable();
	InitPfpTable();
	InitMessageSilhouettes();
}
void RegisterChatWindow(GtkWidget* Window, char* forWho) {
	g_hash_table_insert(chatwndmap, forWho, Window);
}
void DeregisterChatWindow(char* forWho) {
	g_hash_table_remove(chatwndmap, forWho);
}
GtkWidget* GetChatWindow(char* forWho) {
	return g_hash_table_lookup(chatwndmap, forWho);
}
void InsertUserObject(char* id, YampUser* usr) {
	g_hash_table_replace(dtt, id, usr);
}
YampUser* GetUserObject(char* id) { return g_hash_table_lookup(dtt, id); }
void InsertConversationObject(char* id, YampChannel* usr) {
	g_hash_table_replace(ctt, id, usr);
}
YampChannel* GetConversationObject(char* id) { return g_hash_table_lookup(ctt, id); }
void InsertSpaceObject(char* id, YampSpace* object) {
	g_hash_table_insert(stt, id, object);
}
YampSpace* GetSpaceObject(char* id) { return g_hash_table_lookup(stt, id); }

void UpdateSpaceChannelCache(YampChannel **channels, int count, const char *space) {
    SpaceChannelCache *c = g_new0(SpaceChannelCache, 1);
    c->channels = DupChannelArray(channels,count);
    c->count    = count;
    g_hash_table_replace(space_channel_cache, g_strdup(space), c);
}

int GetSpaceChannelCache(char *space, YampChannel*** out) {
    SpaceChannelCache *c = g_hash_table_lookup(space_channel_cache, space);
	if(!c){
		*out = NULL;
		return 0; // Uncached
	}
	if(out){
		*out=c->channels;
	}
    return c->count;
}

void UpdateChannelMessageCache(YampMessage **msgs, int count, const char *channel) {
    ChMsgCache *c = g_new0(ChMsgCache, 1);
    c->messages = DupMessageArray(msgs,count);
    c->count    = count;
    g_hash_table_replace(space_channel_cache, g_strdup(channel), c);
}

int GetChannelMessageCache(char *space, YampMessage*** out) {
    ChMsgCache *c = g_hash_table_lookup(space_channel_cache, space);
	if(!c){
		*out = NULL;
		return 0; // Uncached
	}
	if(out){
		*out=c->messages;
	}
    return c->count;
}

void InsertPfpPath(char* id, char* path) {
	g_hash_table_replace(pfpmap, g_strdup(id), g_strdup(path));
}

char* GetPfpPath(char* id) {
	return g_hash_table_lookup(pfpmap, id);
}
static int nextmsgid = 0;

int RegisterMessageSilhouette(GtkWidget *msgrow) {
    MessageSilhouette *silhouette = malloc(sizeof(*silhouette));

    silhouette->index = nextmsgid++;
    silhouette->msgrow = msgrow;

    g_hash_table_insert(
        MessageSilhouettes,
        GINT_TO_POINTER(silhouette->index),
        silhouette
    );

    return silhouette->index;
}
GtkWidget *GetMessageSilhouettes(int index) {
    MessageSilhouette *silhouette = g_hash_table_lookup(
        MessageSilhouettes,
        GINT_TO_POINTER(index)
    );

    if (!silhouette)
        return NULL;

    return silhouette->msgrow;
}