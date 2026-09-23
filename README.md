# sdl-rdp

SDL3 with opt-in `rdp` video and playback audio drivers. An unmodified SDL program on a
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
update SDL's desktop display mode. Borderless fullscreen follows that screen;
exclusive fullscreen keeps its selected size. Leaving fullscreen restores the
windowed size. Client-side smart sizing can stretch the picture to the client's screen.

For exclusive fullscreen, select an advertised mode with
`SDL_GetFullscreenDisplayModes`, pass it to
`SDL_SetWindowFullscreenMode(window, &mode)`, then call
`SDL_SetWindowFullscreen(window, true)`. The driver advertises the client screen
and these sizes at the client's refresh rate:

320x200, 320x240, 320x256, 400x300, 512x384, 640x350, 640x400, 640x480,
720x400, 720x480, 800x600, 1024x768, 1280x720, 1280x800, 1920x1080,
1920x1200, 2560x1440, 3840x2160.

Sample flags: `--fullscreen --mode 320x200` selects exclusive fullscreen,
`--fullscreen` selects borderless, `--size 640x480` sets the windowed size,
and `--aspect 4:3` declares the picture's display aspect. F4 toggles fullscreen.
`--partial` submits only the animated strip between initial and exposed/resized full frames.

`SDL_RDP_CODEC` accepts `auto` (default), `remotefx`, `nscodec`, `planar`,
`raw`, and `progressive`. On legacy connections, auto selects RemoteFX, then NSCodec, then planar, then raw among
negotiated codecs. An unsupported explicit preference uses that same fallback.
Planar is the explicit lossless choice; RemoteFX and NSCodec are lossy.
The hint can change live; `SDL_PROP_WINDOW_RDP_CODEC_STRING` reports the
negotiated codec on the window.

Measured legacy title-screen traffic at 1280x800 explains the auto preference:

| Codec | Wire traffic |
|---|---:|
| RemoteFX | 3.5 MB/s |
| Planar | 91.6 MB/s |
| Raw | 253 MB/s |

Session facts arrive as native SDL events: a client attaching is
EXPOSED + FOCUS_GAINED, leaving is OCCLUDED + FOCUS_LOST, the display mode
reports the client's screen. The desktop mode keeps the refresh known at connect
and at each screen change. Live refresh estimates update only the current mode,
which keeps the selected fullscreen size while exclusive fullscreen is active.
Details are properties (bound port on the display, client name on the window). A failed open is in `SDL_GetError()`,
backend diagnostics go to `SDL_Log`.

## Graphics pipeline

Clients that negotiate MS-RDPEGFX use `progressive` by default. `planar` and
`raw` select exact RGB transport; `auto`, `progressive`, `remotefx`, and `nscodec`
select progressive on the pipeline. RemoteFX and NSCodec remain legacy choices.
Clients without GFX, with a rejected channel, or without confirmation within
three seconds of activation use legacy codec selection. The connected event and `SDL_PROP_WINDOW_RDP_CODEC_STRING`
report the negotiated codec; both sides need ABI 6.

Progressive encodes damaged tiles with FreeRDP's single-pass RemoteFX encoder,
without refinement passes. SYNC/CONTEXT headers are sent once per surface.
Resize replaces the context and surface and sends a full picture without deactivating the session; pointer PDUs continue.

At most two frames await acknowledgement. This project's byte-budget rule also
limits pending payload bytes + nonzero client `queueDepth` + next payload to
max(64 KiB, twice the larger of the last and next payloads), excluding TLS/DVC
headers. With no outstanding frames, one probe is allowed for stale backlog
reports. The suspend value clears outstanding frames and disables waiting until
another acknowledgement arrives. Capabilities are logged at INFO once per peer;
`SDL_LOGGING=video=info` shows them in SDL applications.

| Client | Progressive | AVC420 (round two) | AVC444 |
|---|---|---|---|
| Windows 10/11 mstsc | Yes | Yes | Yes, including v2 |
| macOS Windows App / Microsoft Remote Desktop | Yes | Yes | Unverified |
| iOS / Android Windows App | Assumed; unverified | Unsupported in xrdp tests | Unsupported in xrdp tests |
| FreeRDP 3.15 | Yes | Yes | Yes |

Round two will add AVC420 through NVENC with capability-based selection.

## Authentication

`SDL_RDP_AUTH` selects `none`, `tls`, or `nla`. `none` preserves the default:
TLS and standard RDP security, with no credential checks. `tls` accepts only TLS
and verifies the plain credentials in Client Info. `nla` offers CredSSP/NTLM
and also accepts TLS-only clients; NLA checks the NT hash first, then verifies
the delegated plain credentials. A password hint defaults the driver to `nla`;
without it the default is `none`. Backend configs default to `SDLRDP_AUTH_NONE`.

Set `SDL_HINT_RDP_USER`, `SDL_HINT_RDP_PASSWORD`, and optionally
`SDL_HINT_RDP_DOMAIN` before initializing audio or video (environment names
`SDL_RDP_USER`, `SDL_RDP_PASSWORD`, `SDL_RDP_DOMAIN`). An unset domain accepts
any domain; a set domain must match exactly.
The `SDL_RDP_PASSWORD` environment variable is readable by other processes of the same user; set the password hint from code as an alternative. Set `SDL_HINT_RDP_AUTH` to override
the default. The sample accepts `--user <u> --password <p> [--domain <d>]
[--auth none|tls|nla]`; `--verify-deny` exercises application rejection.

After `SDL_Init`, an app can set these display pointer properties:

- `SDL_PROP_DISPLAY_RDP_VERIFY_POINTER`:
  `bool (SDLCALL *)(void *userdata, const char *domain, const char *user, const char *password)`.
- `SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER`:
  `bool (SDLCALL *)(void *userdata, const char *domain, const char *user, Uint8 nt_hash[16])`.
- `SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER`: shared callback userdata.

Callbacks run on the peer worker thread. The driver reads properties on each
call. Keep callbacks and userdata alive until shutdown and synchronize mutable
application state. Callbacks take precedence over the fixed hint pair. In TLS
and NLA modes, missing verification rejects; missing lookup rejects NLA while
TLS-only clients still reach verification. `none` does not invoke verification.
Use an explicit authentication hint with property callbacks. The listener opens
during init; install properties before allowing clients to connect. Audio-only
use supports the fixed hints but has no display for callback properties.

On connection, read `SDL_PROP_WINDOW_RDP_USER_STRING`,
`SDL_PROP_WINDOW_RDP_DOMAIN_STRING`, and
`SDL_PROP_WINDOW_RDP_AUTHENTICATED_BOOLEAN`. Names are UTF-8; `none` still reports
the Client Info name with authenticated=false. Passwords never enter events or
logs. Rejections produce one backend WARN `Authentication rejected: user
"<domain\user>" from <address>`; the sample also prints `event AUTH_REJECTED
user=<u>`. Successful sample sessions print `event CONNECTED user=<u>
domain=<d> authenticated=<0|1>`.

mstsc and the Mac Remote Desktop client prompt for NLA credentials.
With xfreerdp use `/sec:nla` or `/sec:tls` and `/u:`, `/p:`, `/d:`.
There is no lockout, PAM integration, or Kerberos authentication.

Backend ABI 6 adds authentication and identity fields. The former log userdata
field is now `log_user`; `user` is the fixed username. Config credential strings
are copied by open. Callback pointers and `auth_user` must live through close.
`sdlrdp_verify_pair` and `sdlrdp_lookup_pair` expose the same fixed-pair fallback
for the dynamically loaded SDL driver.

## Clipboard

Clipboard text travels both ways through SDL's `SDL_SetClipboardText`,
`SDL_GetClipboardText`, and `SDL_HasClipboardText`. Remote changes arrive as
`SDL_EVENT_CLIPBOARD_UPDATE`. The sample accepts `--clip "hello"` and prints
`event CLIPBOARD text=<text>` on updates. UTF-8 text is transferred as
`CF_UNICODETEXT` (UTF-16LE); the server also announces `CF_TEXT`
with ASCII fallback (`?` for non-ASCII characters).
Clipboard redirection must be enabled in the RDP client (`/clipboard` in
xfreerdp). Images and files are not supported yet.

The backend ABI is version 6; graphics adds `SDLRDP_CODEC_PROGRESSIVE`. Clipboard provides `sdlrdp_set_clipboard_text`,
`sdlrdp_get_clipboard_text`, and `sdlrdp_has_clipboard_text`. The getter's
pointer belongs to the handle; copy it before another clipboard API call.

## Audio

Select playback with `SDL_AUDIO_DRIVER=rdp`. The single playback device,
"RDP client", starts at 48 kHz, 16-bit stereo PCM and switches to the
client’s negotiated rate (48 kHz or 44.1 kHz). SDL converts application
streams to the current device format. `SDL_RDP_AUDIO_LATENCY` controls how far
the server may run ahead of the client’s confirmed playback before it waits, in milliseconds (default 500).
This window guards against a stalled client; the SDL driver paces the stream.
Audio is sent in 20 ms blocks. The driver maintains a real-time
audio clock even when no client is attached, discarding those samples.
The backend does no rate conversion: `sdlrdp_audio_open(handle)` opens playback,
`sdlrdp_audio_rate(handle)` returns the negotiated rate (0 without a playing
client), and `sdlrdp_audio_write` accepts stereo S16 frames at that rate.
`SDLRDP_AUDIO {freq, 1}` announces audio negotiation; `{0, 0}` announces loss
of audio. A client advertising no compatible formats receives no audio; the
sound channel is released, `{0, 0}` is queued, and the audio rate stays zero.
Video, input and clipboard continue on the same connection.
Connection events are never revised after they are queued.
Playback uses FreeRDP’s `SendSamples2` to send PCM directly as Wave2, requiring
rdpsnd version 8 or newer. FreeRDP’s DSP is not used and no wire correction is applied.
The sample logs the device format again when SDL reports a format change.

Audio works without initializing video. An audio-only application opens
the same listener using the RDP hints above, with a black desktop at the
configured width and height. Audio and video share a reference-counted
backend handle when both are selected. There is no recording device.

For mstsc, leave Remote audio playback set to **Play on this computer**
(the default). Run the sample with `--tone` for a 440 Hz sine at -12 dBFS;
add `--tight` to exercise audio alongside frame acknowledgement pacing:

    SDL_VIDEO_DRIVER=rdp SDL_AUDIO_DRIVER=rdp sdl-rdp-sample --tone --tight

## Consuming

`buildutil publish` pushes `sdl-rdp` to the site conan remote. A project takes
it with one `Require` line and links the `SDL3` component; the backend is never
linked, only found next to it at runtime.

## Input

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
ships no portable Windows-layout-to-character table. FreeRDP 3.15 has no
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

## Drives

Share a folder in the RDP client: `/drive:share,/path/to/folder` in xfreerdp,
**Local resources, Drives** in mstsc, or **Folders** in the Mac client.
The application on the box initiates every file operation against that share.

For whole files, set `SDL_HINT_STORAGE_TITLE_DRIVER` to `"rdp"`, then call
`SDL_OpenTitleStorage("share", 0)` and `SDL_StorageReady`. An empty override
selects the first shared drive. This title storage supports reads and writes,
directory enumeration, metadata, mkdir, remove, rename and copy. Remaining
space is reported as zero (unknown). `SDL_HINT_STORAGE_USER_DRIVER="rdp"`
also selects it; the application argument names the drive and the organization
argument is unused. Storage uses the same backend listener as video and audio.

For random access, get `SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER` from
`SDL_GetDisplayProperties(SDL_GetPrimaryDisplay())`. Its type is:

```c
typedef SDL_IOStream *(SDLCALL *RDP_OpenFile)(const char *drive,
                                           const char *path,
                                           const char *mode);
```

Call it with `("share", "disk.img", "r+b")`, then use `SDL_SeekIO`,
`SDL_ReadIO`, `SDL_WriteIO`, `SDL_GetIOSize` and `SDL_CloseIO` normally.
Modes follow `SDL_IOFromFile`; paths are UTF-8 with `/`. Operations block
until the client responds or disconnects. There is no cache and no FUSE yet.
Flush waits for a metadata round trip after acknowledged writes; it does not
promise that the client's operating system has flushed physical media.

`SDL_PROP_DISPLAY_RDP_DRIVES_STRING` contains drive names separated by newlines,
updated when the application pumps SDL events. There is no native SDL signal
for drive changes and no custom event; read the property when needed.
Disconnected streams fail; reconnecting clients receive fresh drive IDs.
Close streams before shutting down SDL, and coordinate a stream's position and
lifetime when sharing it between application threads.

Backend ABI version 6 adds `SDLRDP_DRIVE` announcements and `sdlrdp_drive_*`.
Enumeration uses an entry offset: add the returned count to the offset for the
next page. A directory changed between calls may reorder entries. Modification
times are Unix seconds. Drive names have a 511-byte UTF-8 limit and entry names
1023 bytes. Each read/write ABI call accepts at most `INT_MAX` bytes, with
64-bit file offsets, and sends up to eight 64 KiB requests concurrently.
Calls from different threads can be outstanding together. The peer owns the
static channel; transport loss wakes all waiters and removes its drives.

FreeRDP 3.15's server `DriveReadFile`/`DriveWriteFile` wrappers expose 32-bit
offsets and a private reader thread. This backend instead pumps MS-RDPEFS
packets through FreeRDP's WTS channel on the peer and owns completion IDs,
preserving 64-bit offsets and avoiding races with directory continuations.
The 64 KiB chunk size is this backend's transfer limit, not a negotiated
client maximum; FreeRDP 3.15's drive reader accepts a 32-bit Length field
without an explicit smaller read cap.

The sample accepts `--ls share[/path]`, `--cat share/path` and
`--write share/path`. The last command writes a 1 MiB pattern (`i % 251`)
at offsets zero and 2 MiB. The cat command prints a SHA-256 via OpenSSL.
