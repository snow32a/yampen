include config.mk

SRC = *.c protocol/*.c
CC = gcc

CFLAGS += -Wall
LIBS += -lrsvg-2 -lcurl -lcjson -lgtk-4 -lpangocairo-1.0 -lpango-1.0 \
        -lharfbuzz -lgdk_pixbuf-2.0 -lcairo-gobject -lcairo \
        -lvulkan -lgraphene-1.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0 \
        -pthread

yampen: $(SRC)
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LIBS)