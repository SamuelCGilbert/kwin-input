// SPDX-License-Identifier: GPL-3.0-or-later
// kwin-input: send keyboard and mouse input on KDE Plasma (Wayland) through
// KWin's org_kde_kwin_fake_input protocol. No root, no uinput.
//
// KWin only exposes org_kde_kwin_fake_input to executables listed in a
// .desktop file with "X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input" and
// an Exec= line that points at this binary.
//
// Usage: kwin-input [--clearmodifiers] [--delay MS] COMMAND [ARGS] [COMMAND...]
//   key COMBO...        press and release, e.g. ctrl+shift+Tab, Escape, super
//   keydown COMBO       press only
//   keyup COMBO         release only
//   type TEXT           type text (characters must exist in the keyboard layout)
//   click BUTTON        left|middle|right|back|forward or 1-9
//   mousedown BUTTON / mouseup BUTTON
//   move X Y            absolute pointer position in logical coordinates
//   moveby DX DY        relative pointer motion
//   scroll DY [DX]      wheel steps (positive = down / right)
//   sleep MS
//   -                   read commands from stdin, one per line (prints "ok")
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "fake-input-client-protocol.h"

static struct org_kde_kwin_fake_input *fake;
static uint32_t fake_version;
static struct wl_display *display;
static int delay_ms = 2;

static void sleep_ms(int ms) {
  if (ms <= 0) return;
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
  }
}

static void global(void *data, struct wl_registry *reg, uint32_t name,
                   const char *iface, uint32_t version) {
  (void)data;
  if (strcmp(iface, org_kde_kwin_fake_input_interface.name) == 0) {
    fake_version = version < 6 ? version : 6;
    fake = wl_registry_bind(reg, name, &org_kde_kwin_fake_input_interface,
                            fake_version);
  }
}
static void global_remove(void *d, struct wl_registry *r, uint32_t n) {
  (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener registry_listener = {global,
                                                              global_remove};

static void flush(void) {
  wl_display_roundtrip(display);
  sleep_ms(delay_ms);
}

static xkb_keysym_t keysym_from(const char *name) {
  static const struct { const char *alias, *name; } aliases[] = {
      {"ctrl", "Control_L"},  {"control", "Control_L"}, {"shift", "Shift_L"},
      {"alt", "Alt_L"},       {"super", "Super_L"},     {"meta", "Super_L"},
      {"win", "Super_L"},     {"altgr", "ISO_Level3_Shift"},
      {"enter", "Return"},    {"esc", "Escape"},        {"del", "Delete"},
      {"ins", "Insert"},      {"pgup", "Prior"},        {"pgdn", "Next"},
      {"pageup", "Prior"},    {"pagedown", "Next"},     {"backspace", "BackSpace"},
      {"space", "space"},     {"tab", "Tab"},           {"print", "Print"},
  };
  for (size_t i = 0; i < sizeof aliases / sizeof *aliases; i++)
    if (strcasecmp(name, aliases[i].alias) == 0)
      return xkb_keysym_from_name(aliases[i].name, XKB_KEYSYM_NO_FLAGS);
  xkb_keysym_t sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
  if (sym == XKB_KEY_NoSymbol)
    sym = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
  return sym;
}

static bool send_keysym(xkb_keysym_t sym, bool press) {
  if (fake_version < 6) {
    fprintf(stderr, "kwin-input: KWin is too old (fake_input v6 needed)\n");
    return false;
  }
  org_kde_kwin_fake_input_keyboard_keysym(
      fake, sym,
      press ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
  flush();
  return true;
}

// Parse "ctrl+shift+Tab" into keysyms. A literal "+" key is written "plus".
static int parse_combo(const char *combo, xkb_keysym_t *out, int max) {
  char *copy = strdup(combo), *save = NULL;
  int n = 0;
  for (char *tok = strtok_r(copy, "+", &save); tok && n < max;
       tok = strtok_r(NULL, "+", &save)) {
    xkb_keysym_t sym = keysym_from(tok);
    if (sym == XKB_KEY_NoSymbol) {
      fprintf(stderr, "kwin-input: unknown key '%s'\n", tok);
      free(copy);
      return -1;
    }
    out[n++] = sym;
  }
  free(copy);
  return n;
}

static bool key_combo(const char *combo, bool press, bool release) {
  xkb_keysym_t syms[8];
  int n = parse_combo(combo, syms, 8);
  if (n <= 0) return false;
  if (press)
    for (int i = 0; i < n; i++)
      if (!send_keysym(syms[i], true)) return false;
  if (release)
    for (int i = n - 1; i >= 0; i--)
      if (!send_keysym(syms[i], false)) return false;
  return true;
}

static void clear_modifiers(void) {
  static const char *mods[] = {"Shift_L", "Shift_R",   "Control_L", "Control_R",
                               "Alt_L",   "Alt_R",     "Super_L",   "Super_R",
                               "ISO_Level3_Shift"};
  for (size_t i = 0; i < sizeof mods / sizeof *mods; i++)
    send_keysym(xkb_keysym_from_name(mods[i], XKB_KEYSYM_NO_FLAGS), false);
}

static bool type_text(const char *text) {
  // Decode UTF-8 and send one keysym per character
  const unsigned char *p = (const unsigned char *)text;
  while (*p) {
    uint32_t cp;
    int len;
    if (*p < 0x80) { cp = *p; len = 1; }
    else if ((*p & 0xe0) == 0xc0) { cp = *p & 0x1f; len = 2; }
    else if ((*p & 0xf0) == 0xe0) { cp = *p & 0x0f; len = 3; }
    else { cp = *p & 0x07; len = 4; }
    for (int i = 1; i < len; i++) {
      if ((p[i] & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (p[i] & 0x3f);
    }
    p += len;
    xkb_keysym_t sym;
    if (cp == '\n') sym = XKB_KEY_Return;
    else if (cp == '\t') sym = XKB_KEY_Tab;
    else sym = xkb_utf32_to_keysym(cp);
    if (sym == XKB_KEY_NoSymbol) continue;
    if (!send_keysym(sym, true) || !send_keysym(sym, false)) return false;
  }
  return true;
}

static int button_code(const char *name) {
  static const struct { const char *name; int code; } buttons[] = {
      {"left", BTN_LEFT},   {"1", BTN_LEFT},     {"middle", BTN_MIDDLE},
      {"2", BTN_MIDDLE},    {"right", BTN_RIGHT}, {"3", BTN_RIGHT},
      {"back", BTN_SIDE},   {"8", BTN_SIDE},     {"forward", BTN_EXTRA},
      {"9", BTN_EXTRA},
  };
  for (size_t i = 0; i < sizeof buttons / sizeof *buttons; i++)
    if (strcasecmp(name, buttons[i].name) == 0) return buttons[i].code;
  return -1;
}

static bool button(const char *name, bool press, bool release) {
  int code = button_code(name);
  if (code < 0) {
    fprintf(stderr, "kwin-input: unknown button '%s'\n", name);
    return false;
  }
  if (press) {
    org_kde_kwin_fake_input_button(fake, code, WL_POINTER_BUTTON_STATE_PRESSED);
    flush();
  }
  if (release) {
    org_kde_kwin_fake_input_button(fake, code, WL_POINTER_BUTTON_STATE_RELEASED);
    flush();
  }
  return true;
}

static bool is_command(const char *s) {
  static const char *cmds[] = {"key", "keydown", "keyup", "type", "click", "mousedown",
                               "mouseup", "move", "moveby", "scroll", "sleep"};
  for (size_t i = 0; i < sizeof cmds / sizeof *cmds; i++)
    if (!strcmp(s, cmds[i])) return true;
  return false;
}

// Run one command, args[0] is the command name. Returns the number of
// arguments consumed (including the name) or -1 on error.
static int run(int argc, char **args) {
  const char *cmd = args[0];
#define NEED(n)                                                      \
  if (argc < (n) + 1) {                                              \
    fprintf(stderr, "kwin-input: '%s' needs %d argument(s)\n", cmd, n); \
    return -1;                                                       \
  }
  if (!strcmp(cmd, "key")) {
    NEED(1);
    int i = 1;
    // "key" takes all following arguments that are not commands
    for (; i < argc; i++) {
      if (i > 1 && is_command(args[i])) break;
      if (!key_combo(args[i], true, true)) return -1;
    }
    return i;
  }
  if (!strcmp(cmd, "keydown")) { NEED(1); return key_combo(args[1], true, false) ? 2 : -1; }
  if (!strcmp(cmd, "keyup")) { NEED(1); return key_combo(args[1], false, true) ? 2 : -1; }
  if (!strcmp(cmd, "type")) { NEED(1); return type_text(args[1]) ? 2 : -1; }
  if (!strcmp(cmd, "click")) { NEED(1); return button(args[1], true, true) ? 2 : -1; }
  if (!strcmp(cmd, "mousedown")) { NEED(1); return button(args[1], true, false) ? 2 : -1; }
  if (!strcmp(cmd, "mouseup")) { NEED(1); return button(args[1], false, true) ? 2 : -1; }
  if (!strcmp(cmd, "move")) {
    NEED(2);
    if (fake_version < 3) return -1;
    org_kde_kwin_fake_input_pointer_motion_absolute(
        fake, wl_fixed_from_double(atof(args[1])), wl_fixed_from_double(atof(args[2])));
    flush();
    return 3;
  }
  if (!strcmp(cmd, "moveby")) {
    NEED(2);
    org_kde_kwin_fake_input_pointer_motion(
        fake, wl_fixed_from_double(atof(args[1])), wl_fixed_from_double(atof(args[2])));
    flush();
    return 3;
  }
  if (!strcmp(cmd, "scroll")) {
    NEED(1);
    double dy = atof(args[1]);
    int used = 2;
    double dx = 0;
    if (argc > 2 && (isdigit((unsigned char)args[2][0]) || args[2][0] == '-')) {
      dx = atof(args[2]);
      used = 3;
    }
    // One wheel step is 15 degrees, which KWin reports as 15 units
    if (dy != 0) org_kde_kwin_fake_input_axis(fake, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(dy * 15));
    if (dx != 0) org_kde_kwin_fake_input_axis(fake, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(dx * 15));
    flush();
    return used;
  }
  if (!strcmp(cmd, "sleep")) { NEED(1); sleep_ms(atoi(args[1])); return 2; }
  fprintf(stderr, "kwin-input: unknown command '%s'\n", cmd);
  return -1;
}

static int split(char *line, char **args, int max) {
  int n = 0;
  char *p = line;
  while (*p && n < max) {
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (!*p) break;
    if (*p == '"') {  // quoted argument, \" and \\ escapes
      char *out = ++p;
      args[n++] = out;
      while (*p && *p != '"') {
        if (*p == '\\' && p[1]) p++;
        *out++ = *p++;
      }
      if (*p) p++;
      *out = '\0';
    } else {
      args[n++] = p;
      while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
      if (*p) *p++ = '\0';
    }
  }
  return n;
}

int main(int argc, char **argv) {
  bool clear = false;
  int i = 1;
  for (; i < argc; i++) {
    if (!strcmp(argv[i], "--clearmodifiers")) clear = true;
    else if (!strcmp(argv[i], "--delay") && i + 1 < argc) delay_ms = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      fprintf(stderr, "usage: kwin-input [--clearmodifiers] [--delay MS] "
                      "key COMBO... | keydown/keyup COMBO | type TEXT | click/mousedown/mouseup BUTTON |\n"
                      "       move X Y | moveby DX DY | scroll DY [DX] | sleep MS | - (stdin)\n");
      return 0;
    } else break;
  }
  if (i >= argc) {
    fprintf(stderr, "kwin-input: no command (see --help)\n");
    return 2;
  }

  display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "kwin-input: cannot connect to the Wayland compositor\n");
    return 1;
  }
  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);
  if (!fake) {
    fprintf(stderr,
            "kwin-input: KWin does not allow this program to send input. Install a "
            ".desktop file with Exec=<path to kwin-input> and "
            "X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input\n");
    return 1;
  }
  org_kde_kwin_fake_input_authenticate(fake, "kwin-input", "Send keyboard and mouse input");
  if (clear) clear_modifiers();

  int status = 0;
  if (!strcmp(argv[i], "-")) {
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, stdin) > 0) {
      char *args[64];
      int n = split(line, args, 64);
      bool ok = true;
      for (int j = 0; j < n;) {
        int used = run(n - j, args + j);
        if (used < 0) { ok = false; break; }
        j += used;
      }
      printf(ok ? "ok\n" : "error\n");
      fflush(stdout);
    }
    free(line);
  } else {
    while (i < argc) {
      int used = run(argc - i, argv + i);
      if (used < 0) { status = 1; break; }
      i += used;
    }
  }
  if (fake_version >= 5) org_kde_kwin_fake_input_destroy(fake);
  wl_display_roundtrip(display);
  wl_display_disconnect(display);
  return status;
}
