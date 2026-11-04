PREFIX ?= $(HOME)/.local/opt/kwin-input
CFLAGS ?= -O2 -Wall -Wextra
PKGS = wayland-client xkbcommon

kwin-input: kwin-input.c fake-input-client-protocol.h fake-input-protocol.c
	$(CC) $(CFLAGS) -o $@ kwin-input.c fake-input-protocol.c $$(pkg-config --cflags --libs $(PKGS))

fake-input-client-protocol.h: fake-input.xml
	wayland-scanner client-header $< $@
fake-input-protocol.c: fake-input.xml
	wayland-scanner private-code $< $@

install: kwin-input
	install -Dm755 kwin-input $(PREFIX)/bin/kwin-input
	mkdir -p $(HOME)/.local/share/applications
	sed "s|@BIN@|$(PREFIX)/bin/kwin-input|" kwin-input.desktop.in > $(HOME)/.local/share/applications/kwin-input.desktop

clean:
	rm -f kwin-input fake-input-client-protocol.h fake-input-protocol.c
