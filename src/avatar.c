#include <stdlib.h>
#include <glib.h>
#include <globals.h>
#include <yamp.h>
#define AVATAR_MAX_BYTES (8 * 1024 * 1024)

typedef struct {
	char* url;
	char* cache_path;
	int px;
	GWeakRef widget;
	GBytes* bytes;
} AvatarFetch;

size_t AvatarWriteCb(char* ptr, size_t size, size_t nmemb, void* ud) {
	GByteArray* buf = ud;
	size_t n = size * nmemb;
	if (buf->len + n > AVATAR_MAX_BYTES)
		return 0;
	g_byte_array_append(buf, (guint8*)ptr, n);
	return n;
}
char* AvatarCachePath(const char* url) {
	char* hash = g_compute_checksum_for_string(G_CHECKSUM_SHA1, url, -1);
	char* path = g_build_filename(pfp_dir, hash, NULL);
	g_free(hash);
	return path;
}
gboolean AvatarDone(gpointer data) {
	AvatarFetch* f = data;
	GtkWidget* w = g_weak_ref_get(&f->widget);

	if (w) {
		if (f->bytes) {
			GError* err = NULL;
			GdkTexture* tex = gdk_texture_new_from_bytes(f->bytes, &err);
			if (tex) {
				gtk_image_set_from_paintable(GTK_IMAGE(w), GDK_PAINTABLE(tex));
				if (f->px > 0)
					gtk_image_set_pixel_size(GTK_IMAGE(w), f->px);
				g_object_unref(tex);
			} else {
				g_warning("Bad avatar image from %s: %s", f->url, err->message);
				g_clear_error(&err);
			}
		}
		g_object_unref(w);
	}

	if (f->bytes)
		g_bytes_unref(f->bytes);
	g_weak_ref_clear(&f->widget);
	g_free(f->url);
	g_free(f->cache_path);
	g_free(f);
	return G_SOURCE_REMOVE;
}

gpointer AvatarThread(gpointer data) {
	AvatarFetch* f = data;
	GByteArray* buf = g_byte_array_new();
	gboolean from_cache = FALSE;

	if (f->cache_path) {
		gchar* contents = NULL;
		gsize len = 0;
		if (g_file_get_contents(f->cache_path, &contents, &len, NULL)) {
			if (len > 0 && len <= AVATAR_MAX_BYTES) {
				g_byte_array_append(buf, (guint8*)contents, len);
				from_cache = TRUE;
			}
			g_free(contents);
		}
	}

	if (!from_cache) {
		CURL* c = curl_easy_init();
		if (c) {
			curl_easy_setopt(c, CURLOPT_URL, f->url);
			curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, AvatarWriteCb);
			curl_easy_setopt(c, CURLOPT_WRITEDATA, buf);
			curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
			curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
			CURLcode rc = curl_easy_perform(c);
			curl_easy_cleanup(c);
			if (rc != CURLE_OK) {
				g_warning("Avatar fetch failed (%s): %s", f->url,
						  curl_easy_strerror(rc));
				g_byte_array_set_size(buf, 0);
			}
		}
		if (buf->len && f->cache_path)
			g_file_set_contents(f->cache_path, (gchar*)buf->data, buf->len,
								NULL);
	}

	if (buf->len)
		f->bytes = g_byte_array_free_to_bytes(buf);
	else
		g_byte_array_free(buf, TRUE);

	g_idle_add(AvatarDone, f);
	return NULL;
}
void LoadAvatarAsync(GtkWidget* image, const char* url, const char* cache_path,
					 int px) {
	if (!image || !url || !*url)
		return;
	AvatarFetch* f = g_new0(AvatarFetch, 1);
	f->url = g_strdup(url);
	f->cache_path = g_strdup(cache_path);
	f->px = px;
	g_weak_ref_init(&f->widget, image);
	g_thread_unref(g_thread_new("avatar-fetch", AvatarThread, f));
}

unsigned int GetDefaultPfp(const char* id) {
	unsigned rem = 0;

	for (int i = 0; i < strlen(id); i++) {
		unsigned v;

		if (id[i] >= '0' && id[i] <= '9')
			v = id[i] - '0';
		else if (id[i] >= 'a' && id[i] <= 'z')
			v = id[i] - 'a' + 10;
		else
			return 0; // invalid ID

		rem = (rem * 36 + v) % 5;
	}

	return rem;
}

// version of the same func w/ fallback pfp support
void LoadAvatarAsyncRaw(GtkWidget* image, char* raw_url, char* seed,
						const char* cache_path, int px) {
	if (!raw_url || !strlen(raw_url)) {
		char pfppath[37];
		sprintf(pfppath, "/org/yampen/assets/pfps/default%i.png",
				GetDefaultPfp(seed));
		gtk_image_set_from_resource(GTK_IMAGE(image), pfppath);
		if (px > 0) {
			gtk_image_set_pixel_size(GTK_IMAGE(image), px);
		}
		return;
	}
	char* url = ProcessAttachmentURL(raw_url);
	char* pfp_path = AvatarCachePath(url);
	LoadAvatarAsync(image, url, cache_path, px);
	free(url);
}