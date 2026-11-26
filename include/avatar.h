#pragma once
#include <gtk/gtk.h>
void LoadAvatarAsync(GtkWidget* image, const char* url, const char* cache_path,
					 int px);
unsigned int GetDefaultPfp(const char* id);
void LoadAvatarAsyncRaw(GtkWidget* image, char* raw_url, char* seed,
						const char* cache_path, int px);