#include <yamp.h>
#include <gtk/gtk.h>
#include <imwnd.h>
#include <hashtables.h>
#include <avatar.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdint.h>

typedef struct {
	uint8_t r, g, b;
} Rgb;

int MostCommonColor(GdkPixbuf* pb, Rgb* out) {
	int w = gdk_pixbuf_get_width(pb);
	int h = gdk_pixbuf_get_height(pb);
	int stride = gdk_pixbuf_get_rowstride(pb);
	int nch = gdk_pixbuf_get_n_channels(pb);
	gboolean alpha = gdk_pixbuf_get_has_alpha(pb);
	const guchar* px = gdk_pixbuf_read_pixels(pb);

	static uint32_t count[4096];
	static uint64_t sr[4096], sg[4096], sb[4096];
	memset(count, 0, sizeof count);
	memset(sr, 0, sizeof sr);
	memset(sg, 0, sizeof sg);
	memset(sb, 0, sizeof sb);

	for (int y = 0; y < h; y++) {
		const guchar* p = px + (size_t)y * stride;
		for (int x = 0; x < w; x++, p += nch) {
			if (alpha && p[3] < 128)
				continue; // skip transparent pixels
			int bin = ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4);
			count[bin]++;
			sr[bin] += p[0];
			sg[bin] += p[1];
			sb[bin] += p[2];
		}
	}

	int best = -1;
	uint32_t bestc = 0;
	for (int i = 0; i < 4096; i++)
		if (count[i] > bestc) {
			bestc = count[i];
			best = i;
		}
	if (best < 0)
		return 0;

	out->r = sr[best] / bestc;
	out->g = sg[best] / bestc;
	out->b = sb[best] / bestc;
	return 1;
}
void SetBannerColor(GtkWidget* banner, Rgb c) {
	char css[96];
	snprintf(css, sizeof css, "box.banner { background: rgb(%d,%d,%d); }", c.r,
			 c.g, c.b);

	GtkCssProvider* prov = gtk_css_provider_new();
	gtk_css_provider_load_from_string(prov, css);

	GtkStyleContext* ctx = gtk_widget_get_style_context(banner);
	gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(prov),
								   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	g_object_unref(prov); // the context holds its own reference
}
static void OnPfpPaintable(GObject* img, GParamSpec* ps, gpointer banner) {
	GdkPaintable* p = gtk_image_get_paintable(GTK_IMAGE(img));
	if (!p)
		return;
	GdkPaintable* cur = gdk_paintable_get_current_image(p);
	if (GDK_IS_TEXTURE(cur)) {
		GdkPixbuf* pb = gdk_pixbuf_get_from_texture(GDK_TEXTURE(cur));
		Rgb c;
		if (pb && MostCommonColor(pb, &c))
			SetBannerColor(GTK_WIDGET(banner), c);
		if (pb)
			g_object_unref(pb);
	}
	g_object_unref(cur);
}
void BuildUserProfile(GtkWidget* parent, YampUser* usr) {
	GtkWidget* overlay = gtk_overlay_new();

	GtkWidget* base = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_overlay_set_child(GTK_OVERLAY(overlay), base);

	GtkWidget* pfp;
	if (usr->pfp && strlen(usr->pfp)) {
		pfp = gtk_image_new();
		char* url = ProcessAttachmentURL(usr->pfp);
		char* pfp_path = AvatarCachePath(url);
		LoadAvatarAsync(pfp, url, pfp_path, 80);
		InsertPfpPath(usr->id, pfp_path);
		free(url);
		g_free(pfp_path);
	} else {
		char pfppath[38];
		sprintf(pfppath, "/org/yampen/assets/pfps/default%d.png",
				GetDefaultPfp(usr->id));
		pfp = gtk_image_new_from_resource(pfppath);
	}
	gtk_image_set_pixel_size(GTK_IMAGE(pfp), 80);
	gtk_widget_add_css_class(pfp, "avatar");
	gtk_widget_set_overflow(pfp, GTK_OVERFLOW_HIDDEN);
	GtkWidget* pfpframe = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(pfpframe), pfp);
	gtk_widget_add_css_class(pfpframe, "avatar-gap-frame");
	gtk_widget_set_overflow(pfpframe, GTK_OVERFLOW_HIDDEN);
	gtk_widget_set_halign(pfpframe, GTK_ALIGN_START);
	gtk_widget_set_valign(pfpframe, GTK_ALIGN_END);
	gtk_widget_set_margin_start(pfpframe, 16);

	GtkWidget* banner;
	if (!usr->banner) {
		banner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_add_css_class(banner, "banner");
		Rgb fallback = {0x40, 0x40, 0x40};
		SetBannerColor(banner, fallback);
		g_signal_connect_object(pfp, "notify::paintable",
								G_CALLBACK(OnPfpPaintable), banner, 0);
		OnPfpPaintable(G_OBJECT(pfp), NULL, banner);
		gtk_widget_set_size_request(banner, -1, 100);
		gtk_widget_set_hexpand(banner, TRUE);
		gtk_box_append(GTK_BOX(base), banner);
	} else {
		banner = gtk_picture_new();
		gtk_widget_add_css_class(banner, "banner");
		gtk_widget_set_size_request(banner, -1, 100);
		gtk_widget_set_hexpand(banner, TRUE);
		gtk_box_append(GTK_BOX(base), banner);
	}

	GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(spacer, -1, 40);
	gtk_box_append(GTK_BOX(base), spacer);

	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), pfpframe);

	GtkWidget* infoarea = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_hexpand(infoarea, 1);
	gtk_widget_set_margin_start(infoarea, 16);
	gtk_widget_set_margin_end(infoarea, 16);
	gtk_widget_set_halign(infoarea, GTK_ALIGN_FILL);
	if (usr->displayname) {
		GtkWidget* dpyname = gtk_label_new(usr->displayname);
		gtk_label_set_xalign(GTK_LABEL(dpyname), 0.0f);
		gtk_widget_add_css_class(dpyname, "h2");
		gtk_box_append(GTK_BOX(infoarea), dpyname);
		GtkWidget* usrname = gtk_label_new(usr->username);
		gtk_label_set_xalign(GTK_LABEL(usrname), 0.0f);
		gtk_box_append(GTK_BOX(infoarea), usrname);
	} else {
		GtkWidget* usrname = gtk_label_new(usr->username);
		gtk_widget_add_css_class(usrname, "h2");
		gtk_box_append(GTK_BOX(infoarea), usrname);
	}
	if (usr->description) {
		GtkWidget* bio = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
		gtk_widget_set_hexpand(bio, 1);
		gtk_widget_add_css_class(bio, "section");
        GtkWidget* bioheader = gtk_label_new("Description");
		gtk_label_set_xalign(GTK_LABEL(bioheader), 0.0f);
        gtk_widget_add_css_class(bioheader, "h5");
		GtkWidget* biotext = gtk_label_new(usr->description);
        gtk_label_set_max_width_chars(GTK_LABEL(biotext), 1);
        gtk_label_set_wrap(GTK_LABEL(biotext), GTK_WRAP_WORD);
		gtk_label_set_xalign(GTK_LABEL(biotext), 0.0f);
		gtk_widget_set_hexpand(biotext, 1);
		gtk_box_append(GTK_BOX(bio), bioheader);
		gtk_box_append(GTK_BOX(bio), biotext);
		gtk_box_append(GTK_BOX(infoarea), bio);
	}
	gtk_box_append(GTK_BOX(parent), overlay);
	gtk_box_append(GTK_BOX(parent), infoarea);
}