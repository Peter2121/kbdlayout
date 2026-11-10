# kbdlayout

A small graphical keyboard layout chooser for the FreeBSD console. It needs no
X11 or Wayland: it draws directly on the screen through DRM/KMS, using SDL3
(KMSDRM backend) and Dear ImGui.

- A window centred on the screen lists every console keymap. The layout that
  is active now is selected and marked with a green dot.
- A test field lets you type with the **selected** layout before you apply it,
  including dead keys (`^` then `e` gives `ê`), AltGr, Caps Lock and the second
  layer of Cyrillic and Greek keymaps.
- **OK** loads the keymap with `kbdcontrol -l` and exits. If "Make default at
  boot" is ticked (root only), it also runs `sysrc keymap=xx.kbd`, the same
  setting kbdmap(1) and bsdinstall use.
- **Cancel** exits without changing anything.

Keys: Up/Down/PgUp/PgDn change the selection from any field, Tab moves between
fields, Enter = OK, Escape = Cancel. Mouse and double-click work too.

## Requirements

- FreeBSD with a KMS driver (`i915kms`, `amdgpu`, `nvidia-drm`) and the vt(4)
  console. These are the same conditions as `kmsview` and the SDL3 games.
- Access to `/dev/dri/card*` and `/dev/input/event*` (root, or the `video`
  group with the devfs rule set up earlier).
- A TrueType font for non-ASCII text: `pkg install dejavu` (or `noto-basic`).
  Without one, the built-in ASCII font is used.

## Build

Build dependencies:

    pkg install cmake ninja pkgconf libdrm mesa-libs libudev-devd evdev-proto dejavu

Static build (the default):

    make

The first run downloads SDL 3.4.20 into `deps/` and builds a reduced static
SDL3: KMSDRM video, evdev input through libudev, OpenGL/GLES renderers, no
audio, X11 or Wayland. This takes about a minute. The program is then linked
with SDL3 and Dear ImGui built in. At run time it only needs the base system
libraries (libc, libm, libc++, libthr). libdrm, Mesa (libgbm/libEGL) and
libudev-devd are loaded on demand by SDL, the same way the `sdl3` package does
it.

A completely static binary (`-static`) is not possible: SDL has to load the
Mesa GPU drivers at run time, and static FreeBSD binaries cannot load shared
libraries.

Quick build against the system SDL3 package instead:

    pkg install sdl3
    make shared

Install and tests:

    make install        # /usr/local/sbin/kbdlayout
    make test           # parser and translator tests on /usr/share/vt/keymaps

## Run

From a text console (Ctrl+Alt+F2 when X is running):

    kbdlayout                     # choose, test, apply
    kbdlayout -n                  # dry run: print the commands instead
    kbdlayout -L fr               # descriptions in French (default: $LANG)
    kbdlayout -f /path/font.ttf   # use another font

Exit status: 0 = layout applied, 1 = cancelled, 2 = error.

## How it works

| File | Purpose |
|---|---|
| `src/keymap.cpp` | `.kbd` parser written to match kbdcontrol's lexer and grammar; `INDEX.keymaps` descriptions; a key translator that reproduces the kernel's `genkbd_keyaction()` (Shift/Ctrl/Alt columns, Caps/Num lock flags, dead keys and accent tables, alt-lock layer at keycode+128). |
| `src/kernel_keymap.cpp` | Reads the loaded keymap with the `GIO_KEYMAP` ioctl. The active layout is found by comparing it entry by entry with every `.kbd` file; if that fails, `keymap=` from rc.conf is used. |
| `src/main.cpp` | SDL3 + ImGui interface. SDL scancodes are converted to FreeBSD keycodes and passed to the translator. While the test field has focus, its characters come from the translator instead of SDL, so they follow the selected layout and not the loaded one. The new keymap is only loaded after `SDL_Quit()`, once SDL has given the keyboard back to the console. |

Testing options (also useful off FreeBSD with `SDL_VIDEO_DRIVER=offscreen`):
`-d DIR` keymap directory, `-p FILE` preselect, `-t TEXT` type text as
US-positional keys (`~` = CapsLock, `\n` = Enter), `-s shot.bmp` save a
screenshot and exit.

Dear ImGui (MIT) is included in `third_party/imgui`. SDL3 (zlib licence) is
downloaded at build time.
