PREFIX ?= $(HOME)/.local/opt/kwin-input
# Where the .desktop file that gives kwin-input its KWin permissions goes
APPDIR ?= $(HOME)/.local/share/applications
CFLAGS ?= -O2 -Wall -Wextra
PKGS = wayland-client xkbcommon gio-2.0 gio-unix-2.0

kwin-input: kwin-input.c kwin-query.c kwin-query.h fake-input-client-protocol.h fake-input-protocol.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ kwin-input.c kwin-query.c fake-input-protocol.c -lpthread $$(pkg-config --cflags --libs $(PKGS))

fake-input-client-protocol.h: fake-input.xml
	wayland-scanner client-header $< $@
fake-input-protocol.c: fake-input.xml
	wayland-scanner private-code $< $@

install: kwin-input
	install -Dm755 kwin-input $(DESTDIR)$(PREFIX)/bin/kwin-input
	mkdir -p $(DESTDIR)$(APPDIR)
	sed "s|@BIN@|$(PREFIX)/bin/kwin-input|" kwin-input.desktop.in > $(DESTDIR)$(APPDIR)/kwin-input.desktop

clean:
	rm -f kwin-input fake-input-client-protocol.h fake-input-protocol.c
