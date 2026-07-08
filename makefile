SRC = *.c protocol/*.c
CC = gcc
yampen: $(SRC)
	$(CC) -o yampen $(SRC) -lcurl -lcjson -lgtk-4 -lpangocairo-1.0 -lpango-1.0 -lharfbuzz -lgdk_pixbuf-2.0 -lcairo-gobject -lcairo -lvulkan -lgraphene-1.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0 -pthread -I/usr/include/gtk-4.0 -I/usr/include/pango-1.0 -I/usr/include/fribidi -I/usr/include/harfbuzz -I/usr/include/gdk-pixbuf-2.0 -I/usr/include/webp -I/usr/include/cairo -I/usr/include/libxml2 -I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/pixman-1 -I/usr/include/graphene-1.0 -I/usr/lib64/graphene-1.0/include -mfpmath=sse -msse -msse2 -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -DWITH_GZFILEOP -I/usr/include/libmount -I/usr/include/blkid -I/usr/include/sysprof-6 -pthread
