# kwin-input

Send keyboard and mouse input and take screenshots on KDE Plasma (Wayland),
from the command line or from another program. It needs no root and no uinput.

Wayland doesn't let clients inject input or read the screen. KWin offers both to
programs it trusts:

| What | How |
|---|---|
| Keyboard and mouse input | `org_kde_kwin_fake_input` Wayland protocol |
| Screenshots | `org.kde.KWin.ScreenShot2` D-Bus interface |
| Pointer position, outputs, windows | KWin scripting |

KWin only trusts executables listed in a `.desktop` file with the matching
`X-KDE-Wayland-Interfaces` and `X-KDE-DBUS-Restricted-Interfaces` keys, so
`make install` writes one. Log out and back in after installing, or KWin won't
see it. No permission prompt appears, so any program that can run `kwin-input`
can see your screen and type for you.

## Build and install

Needs `libwayland-dev`, `libwayland-bin` (`wayland-scanner`), `libxkbcommon-dev`
and `libglib2.0-dev`.

```sh
make
make install   # into ~/.local/opt/kwin-input, PREFIX=... to change
```

The `.desktop` file goes to `~/.local/share/applications`.

## Usage

```sh
kwin-input key ctrl+shift+Tab
kwin-input type "hello"
kwin-input move 100 200 click left
kwin-input --clearmodifiers key ctrl+v
kwin-input capture 0 0 1920 1080 out.raw   # raw BGRA, prints width height stride format
kwin-input pointer
kwin-input outputs
kwin-input windows
kwin-input activate '{window-id}'
```

`kwin-input -` reads commands from stdin, one per line, and prints `ok` after
each one. Programs can keep it running this way instead of starting it for each
action. The comment at the top of `kwin-input.c` lists every command.

`monitor` (reads your real input, for recorders) and `--clearmodifiers` for keys
you physically hold need the
[AutoKey KWin bridge](https://github.com/TheSquanch-147/autokey-kwin-bridge) plugin.

## Limits

- Only KDE Plasma. Tested on Plasma 6.7.5.
- `type` only types characters that exist in the active keyboard layout.
- Coordinates are logical, not physical pixels.

## License

GPL-3.0-or-later. `fake-input.xml` comes from KDE's plasma-wayland-protocols
(LGPL-2.1-or-later).
