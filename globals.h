#pragma once
#include <gtk/gtk.h>
#include <curl/curl.h>
#include <openssl/ssl.h>
extern int mainfd;
extern SSL* mainsock;
extern GtkApplication *global_app;
extern char *curUsername;
extern GQueue *spaces_queue;
extern CURL *curl;
