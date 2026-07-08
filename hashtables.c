#include <glib.h>
#include "hashtables.h"
#include <gtk/gtk.h>
GHashTable *dtt;
GHashTable *chatwndmap;
GHashTable *desctt;
GHashTable *pfpmap;
void InitDisplayNameTable() { dtt = g_hash_table_new(g_str_hash, g_str_equal); }
void InitChatWindowTable() { chatwndmap = g_hash_table_new(g_str_hash, g_str_equal); }
void InitDescriptionTable() { desctt = g_hash_table_new(g_str_hash, g_str_equal); }
void InitPfpTable() { pfpmap = g_hash_table_new(g_str_hash, g_str_equal); }
void InitAllTables() {
	InitDisplayNameTable();
	InitDescriptionTable();
	InitChatWindowTable();
	InitPfpTable();
}
void RegisterChatWindow(GtkWidget *Window, char *forWho) {
	g_hash_table_insert(chatwndmap, forWho, Window);
}
void DeregisterChatWindow(char *forWho) {
	g_hash_table_remove(chatwndmap, forWho);
}
GtkWidget* GetChatWindow(char *forWho) {
	return g_hash_table_lookup(chatwndmap,forWho);
}
char *InsertDisplayName(char *username, char *display_name) {
	g_hash_table_insert(dtt, username, display_name);
}
char *GetDisplayName(char *username) {
	return g_hash_table_lookup(dtt,username);
}
char *InsertProfileDescription(char *username, char *display_name) {
	g_hash_table_insert(desctt, username, display_name);
}
char *GetProfileDescription(char *username) {
	return g_hash_table_lookup(desctt,username);
}
char *InsertPfpPath(char *username, char *display_name) {
	g_hash_table_insert(pfpmap, username, display_name);
}
char *GetPfpPath(char *username) {
	return g_hash_table_lookup(pfpmap,username);
}
