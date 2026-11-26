include config.mk

SRC = src/*.c protocol/*.c

RES_DEPS := assets.gresource.xml $(shell glib-compile-resources \
    --sourcedir=. --generate-dependencies assets.gresource.xml 2>/dev/null)

CC = gcc

CFLAGS += -Wall -g -O0 -Iprotocol -Iinclude

BUILD_DIR := build
ifeq ($(PLATFORM),win32)

build/yampen.exe: $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CROSS_COMPILE)$(CC) $(CPPFLAGS) $(CFLAGS) \
		-o $(BUILD_DIR)/yampen.exe \
		$(SRC) $(LDFLAGS) $(LIBS)
	@queue=$$(mktemp); \
	seen=$$(mktemp); \
	trap 'rm -f "$$queue" "$$seen"' EXIT; \
	echo "$(BUILD_DIR)/yampen.exe" > "$$queue"; \
	while [ -s "$$queue" ]; do \
		file=$$(head -n 1 "$$queue"); \
		sed -i '1d' "$$queue"; \
		name=$$(basename "$$file"); \
		if grep -Fxq "$$name" "$$seen"; then \
			continue; \
		fi; \
		echo "$$name" >> "$$seen"; \
		echo "Scanning: $$name"; \
		deps=$$(objdump -p "$$file" 2>/dev/null | \
			sed -n 's/^[[:space:]]*DLL Name: //p'); \
		for dep in $$deps; do \
			case "$$dep" in \
				kernel32.dll|kernelbase.dll|ntdll.dll| \
				user32.dll|gdi32.dll|advapi32.dll|shell32.dll| \
				ole32.dll|oleaut32.dll|ws2_32.dll| \
				secur32.dll|crypt32.dll|bcrypt.dll) \
					continue ;; \
			esac; \
			src="$(MINGW_SYSROOT)/bin/$$dep"; \
			dst="$(BUILD_DIR)/$$dep"; \
			if [ -f "$$src" ] && [ ! -f "$$dst" ]; then \
				echo "  + $$dep"; \
				cp "$$src" "$$dst"; \
				echo "$$dst" >> "$$queue"; \
			fi; \
		done; \
	done
	makensis setup.nsi
else
build/yampen: $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -fsanitize=address $(CFLAGS) \
		-o $(BUILD_DIR)/yampen \
		$(SRC) $(LDFLAGS) $(LIBS)
endif

src/assets.c: $(RES_DEPS)
	./gen-resources.sh
	glib-compile-resources assets.gresource.xml \
	    --sourcedir=. --target=src/assets.c --generate-source