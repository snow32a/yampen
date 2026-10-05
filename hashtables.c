#include <glib.h>
#include "hashtables.h"
#include <gtk/gtk.h>
#include "protocol/yamp.h"
GHashTable* dtt;
GHashTable* stt;
GHashTable* chatwndmap;
GHashTable* desctt;
GHashTable* pfpmap;
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
	dtt = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, FreeUserObj);
}
void InitSpaceIDTable() {
	stt = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, FreeSpaceObj);
}
void InitChatWindowTable() {
	chatwndmap = g_hash_table_new(g_str_hash, g_str_equal);
}
void InitDescriptionTable() {
	desctt = g_hash_table_new(g_str_hash, g_str_equal);
}
void InitPfpTable() { pfpmap = g_hash_table_new(g_str_hash, g_str_equal); }
void InitAllTables() {
	InitUserIDTable();
	InitSpaceIDTable();
	InitDescriptionTable();
	InitChatWindowTable();
	InitPfpTable();
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
	g_hash_table_insert(dtt, id, usr);
}
YampUser* GetUserObject(char* id) { return g_hash_table_lookup(dtt, id); }
void InsertSpaceObject(char* id, YampSpace* object) {
	g_hash_table_insert(stt, id, object);
}
YampSpace* GetSpaceObject(char* id) { return g_hash_table_lookup(stt, id); }
char* InsertProfileDescription(char* username, char* display_name) {
	g_hash_table_insert(desctt, username, display_name);
}
char* GetProfileDescription(char* username) {
	return g_hash_table_lookup(desctt, username);
}
char* InsertPfpPath(char* username, char* display_name) {
	g_hash_table_insert(pfpmap, username, display_name);
}
char* GetPfpPath(char* username) {
	return g_hash_table_lookup(pfpmap, username);
}
