# sdl-rdp

SDL3 with an opt-in `rdp` video driver. An unmodified SDL program on a
headless machine, started with `SDL_VIDEO_DRIVER=rdp`, has its window served
over RDP: a Windows or Mac RDP client is its display, keyboard and mouse.

## Shape

One buildutil project. `./buildutil build`, `./buildutil test`,
`./buildutil publish` at the root.

SDL defaults to `[options] sdl_version = "3.4.8"` in `buildutil.toml`.
Select another pinned release with `./buildutil build --option sdl_version=3.4.16`.
The hashes in `sources/SDL3.so/versions.toml` allow supported releases; each
version keeps its archive, patched tree and configure cache under
`_build/generated/SDL3/<version>/`.

- `sources/SDL3.so/` builds `libSDL3.so`, the library applications link.
  `rdp/` is the driver, ordinary C in SDL's conventions with no FreeRDP
  dependency. `rdp-driver.patch` registers it in SDL's build and bootstrap
  list. `configure.py` is buildutil's per-module hook: it downloads the
  selected SDL release into the generated folder once, applies the patch
  there, runs SDL's own CMake configure (never its build) to learn the file
  list, defines and `SDL_build_config.h` of a headless Linux build, and hands
  those sources to buildutil, which compiles them and the driver into one
  library. Nothing SDL lands in the source tree.
- `sources/sdl-rdp-backend.so/` builds `libsdl-rdp-backend.so`: the FreeRDP 3
  server behind a versioned C ABI (`sdl-rdp-backend.h`), with its gtest
  gate (a headless FreeRDP client connects, frames and input round-trip). The
  driver dlopens it by name only when the `rdp` driver is selected, so SDL
  stays free of FreeRDP and a program runs without the backend installed.
- `sources/sdl-rdp-sample.exe/` is the sample: draws a pattern, prints every
  SDL event as one line.
- `test_package/` consumes the published package the way a downstream
  project does: links the `SDL3` component, starts the `rdp` driver on an
  ephemeral port, and proves the backend resolves from the package.

## Selecting it

Opt in only: probing never picks the driver and no listener opens unasked.

    SDL_VIDEO_DRIVER=rdp        # from the shell, any SDL program
    SDL_HINT_VIDEO_DRIVER       # from code, before SDL_Init: an app's --rdp flag

Driver settings are hints with environment variables of the same name:
`SDL_RDP_PORT` (3389, 0 for ephemeral), `SDL_RDP_BIND` (0.0.0.0),
`SDL_RDP_CERT_DIR` (`$XDG_DATA_HOME/sdl-rdp` or `~/.local/share/sdl-rdp`), `SDL_RDP_WIDTH`, `SDL_RDP_HEIGHT` (1024x768),
`SDL_RDP_WAIT_FOR_CLIENT`, `SDL_RDP_BACKEND` (path of the backend library).
`SDL_RDP_VSYNC` defaults to `1`: surface updates wait up to 100 ms for the
client's frame acknowledgement. Set it to `0` to return immediately.
`SDL_RDP_ASPECT` sets the picture's display aspect (for example `4:3`);
empty means square pixels. It can change live, and
`SDL_PROP_WINDOW_RDP_ASPECT_STRING` reports it on the window.
The window keeps the app's requested size; the RDP desktop is that picture,
with only pixel-aspect correction applied on the server. Client screen changes
update SDL's desktop display mode. A fullscreen window follows that mode.
Client-side smart sizing can stretch the picture to the client's screen.
`SDL_RDP_CODEC` accepts `auto` (default), `remotefx`, `nscodec`, `planar`,
and `raw`. Auto selects RemoteFX, then NSCodec, then planar, then raw among
negotiated codecs. An unsupported explicit preference uses that same fallback.
Planar is the explicit lossless choice; RemoteFX and NSCodec are lossy.
The hint can change live; `SDL_PROP_WINDOW_RDP_CODEC_STRING` reports the
negotiated codec on the window.

Measured title-screen traffic at 1280x800 explains the auto preference:

| Codec | Wire traffic |
|---|---:|
| RemoteFX | 3.5 MB/s |
| Planar | 91.6 MB/s |
| Raw | 253 MB/s |

Session facts arrive as native SDL events: a client attaching is
EXPOSED + FOCUS_GAINED, leaving is OCCLUDED + FOCUS_LOST, the display mode
reports the client's screen and measured refresh rate; details are properties (bound port on the
display, client name on the window). A failed open is in `SDL_GetError()`,
backend diagnostics go to `SDL_Log`.

## Clipboard

Clipboard text travels both ways through SDL's `SDL_SetClipboardText`,
`SDL_GetClipboardText`, and `SDL_HasClipboardText`. Remote changes arrive as
`SDL_EVENT_CLIPBOARD_UPDATE`. The sample accepts `--clip "hello"` and prints
`event CLIPBOARD text=<text>` on updates. UTF-8 text is transferred as
`CF_UNICODETEXT` (UTF-16LE); the server also announces `CF_TEXT`
with ASCII fallback (`?` for non-ASCII characters).
Clipboard redirection must be enabled in the RDP client (`/clipboard` in
xfreerdp). Images and files are not supported yet.

The backend ABI is version 4, adding `sdlrdp_set_clipboard_text`,
`sdlrdp_get_clipboard_text`, and `sdlrdp_has_clipboard_text`. The getter's
pointer belongs to the handle; copy it before another clipboard API call.

## Consuming

`buildutil publish` pushes `sdl-rdp` to the site conan remote. A project takes
it with one `Require` line and links the `SDL3` component; the backend is never
linked, only found next to it at runtime.

## Input

Scancodes produce SDL key events. RDP Unicode keyboard events produce UTF-8
`SDL_EVENT_TEXT_INPUT` while `SDL_StartTextInput(window)` is active; stopping
text input suppresses text without suppressing keys. UTF-16 surrogate pairs
are assembled separately for key-down and key-up, and only down produces text.
The sample starts text input; F2 toggles it and F3 toggles relative mouse mode.

The negotiated Windows keyboard layout ID is reported in backend
`connected.keyboard_layout` and window property `SDL_PROP_WINDOW_RDP_KEYBOARD_LAYOUT_NUMBER`.
SDL's `scancodes_windows.h` maps physical keys, not keyboard layouts. SDL builds
Windows character keymaps using Windows `MapVirtualKey`/`ToUnicode` APIs; it
ships no portable Windows-layout-to-character table. FreeRDP 3.15 has no
`freerdp_keyboard_get_rdp_scancode_from_virtual_key_code` API; WinPR's
`GetVirtualScanCodeFromVirtualKeyCode` maps VKs by keyboard **type**, not layout.
This driver therefore uses SDL's default keymap:
physical scancode A has keycode `a` for every layout, while layout-specific
characters (including accented characters) arrive through Unicode text input.

`SDL_SetWindowRelativeMouseMode` uses relative deltas from FreeRDP's `ainput`
channel when the client supplies them. Otherwise each absolute motion is
measured from the RDP desktop centre and the server sends `PointerPosition`
back to that centre. Centre echoes produce no motion. This fallback requires
the client to honour pointer repositioning; mstsc behavior has not been verified.
Leaving relative mode restores absolute motion. Vertical and horizontal wheels
retain their signed fractions of a 120-unit notch.

The `rdpei` channel supplies an SDL direct-touch device. Contact coordinates
are normalized to the window (the sample also prints window pixel coordinates),
pressure is normalized from 0..1024, and down, move, up and cancellation are
reported as native finger events. Missing pressure defaults to 1.

Backend ABI version 4 adds `sdlrdp_set_relative_mouse`, `SDLRDP_TEXT`,
`SDLRDP_MOUSE_RELATIVE`, `SDLRDP_TOUCH`, and the negotiated layout ID; wheel
components are now floating-point notch counts. Driver and backend must match.
