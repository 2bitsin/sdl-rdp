# Input

Scancodes produce SDL key events and printable key presses also produce UTF-8
`SDL_EVENT_TEXT_INPUT` using SDL’s current keymap and modifiers. RDP Unicode
keyboard events produce key down/up pairs and printable text. Text is emitted
only while `SDL_StartTextInput(window)` is active; stopping
text input suppresses text without suppressing keys. UTF-16 surrogate pairs
are assembled separately for key-down and key-up, and only down produces text.
The sample starts text input; F2 toggles it and F3 toggles relative mouse mode.

The negotiated Windows keyboard layout ID is reported in backend
`connected.keyboard_layout` and window property `SDL_PROP_WINDOW_RDP_KEYBOARD_LAYOUT_NUMBER`.
SDL's `scancodes_windows.h` maps physical keys, not keyboard layouts. SDL builds
Windows character keymaps using Windows `MapVirtualKey`/`ToUnicode` APIs; it
ships no portable Windows-layout-to-character table. FreeRDP 3.32 has no
`freerdp_keyboard_get_rdp_scancode_from_virtual_key_code` API; WinPR's
`GetVirtualScanCodeFromVirtualKeyCode` maps VKs by keyboard **type**, not layout.
This driver uses the keymap SDL holds, falling back to SDL’s default US keymap.
The negotiated layout ID alone does not install a character map. Unicode input
provides layout-specific characters, including accented characters.

Unicode input uses SDL’s `SDL_SendKeyboardUnicodeKey`, as UIKit and OpenVR do.
SDL allocates a reserved scancode for characters absent from its keymap and
sends a key down/up pair. With the default keycode options, `ä` produces
scancode 400 with keycode 0, followed by text `ä`; `a` produces scancode 4,
keycode 97 down/up and text `a`. Escape produces scancode 41, keycode 27
down/up without text (the sample exits on key down). Reserved scancodes can
vary with previously received characters.

`SDL_SetWindowRelativeMouseMode` uses relative deltas from FreeRDP's `ainput`
channel when the client supplies them. Otherwise each absolute motion is
measured from the previous reported position. Near an edge, the server requests
`PointerPosition` back to the desktop centre. Only an immediate centre echo
is treated as a warp and produces no motion; ignored requests preserve normal
deltas. mstsc behavior has not been verified.
Leaving relative mode restores absolute motion. Vertical and horizontal wheels
retain their signed fractions of a 120-unit notch.

The `rdpei` channel supplies an SDL direct-touch device. Contact coordinates
are normalized to the window (the sample also prints window pixel coordinates),
pressure is normalized from 0..1024, and down, move, up and cancellation are
reported as native finger events. Missing pressure defaults to 1.

Backend ABI version 4 adds `sdlrdp_set_relative_mouse`, `SDLRDP_TEXT`,
`SDLRDP_MOUSE_RELATIVE`, `SDLRDP_TOUCH`, and the negotiated layout ID; wheel
components are now floating-point notch counts. Driver and backend must match.
