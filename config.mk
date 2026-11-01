# made by configure - don't edit

UNAME_S := Linux
PLATFORM := win32
CPPFLAGS :=
CFLAGS :=
LDFLAGS :=
LIBS :=
CC :=
MINGW_SYSROOT := /usr/x86_64-w64-mingw32
CC := gcc

CPPFLAGS += -D_WIN32

# In order to force Adwaita Dark on Windows
CPPFLAGS += -D FORCE_ADWAITA_DARK

# GTK4
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include/gtk-4.0

# GLib / GObject / GIO
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include/glib-2.0
CPPFLAGS += -I/usr/x86_64-w64-mingw32/lib/glib-2.0/include

# Pango
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include/pango-1.0

# HarfBuzz
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include/harfbuzz

# GDK Pixbuf
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include/gdk-pixbuf-2.0

# Cairo
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include/cairo

# Graphene
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include/graphene-1.0
CPPFLAGS += -I/usr/x86_64-w64-mingw32/lib/graphene-1.0/include

# librsvg
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include/librsvg-2.0

# libcurl
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include
CPPFLAGS += -DCURL_STATICLIB

# cJSON
CPPFLAGS += -I/usr/x86_64-w64-mingw32/include

LIBS += -L/usr/x86_64-w64-mingw32/lib

LIBS += -Wl,-Bstatic
LIBS += -lcjson
LIBS += -Wl,-Bdynamic

LIBS += -Wl,-Bstatic
LIBS += -lcurl
LIBS += -lssh2
LIBS += -lidn2
LIBS += -lpsl
LIBS += -lunistring
LIBS += -liconv
LIBS += -lssl
LIBS += -lcrypto
LIBS += -lz
LIBS += -lbrotlidec
LIBS += -lbrotlicommon
LIBS += -lzstd
LIBS += -lnghttp2
LIBS += -lngtcp2_crypto_ossl
LIBS += -lngtcp2
LIBS += -lnghttp3
LIBS += -Wl,-Bdynamic

LIBS += -lwldap32
LIBS += -lbcrypt
LIBS += -ladvapi32
LIBS += -lcrypt32
LIBS += -lsecur32
LIBS += -lws2_32
LIBS += -liphlpapi

LIBS += -lgtk-4
LIBS += -lvulkan-1
LIBS += -lgraphene-1.0
LIBS += -lglib-2.0
LIBS += -lgio-2.0
LIBS += -lgobject-2.0
LIBS += -lgdk_pixbuf-2.0
LIBS += -lpangocairo-1.0
LIBS += -lpango-1.0
LIBS += -lharfbuzz
LIBS += -lcairo-gobject
LIBS += -lcairo
LIBS += -lrsvg-2
