# Selecting it

Opt in only: probing never picks the driver and no listener opens unasked.

    SDL_VIDEO_DRIVER=rdp        # from the shell, any SDL program
    SDL_HINT_VIDEO_DRIVER       # from code, before SDL_Init: an app's --rdp flag

Driver settings use these sources in order: an application hint set through SDL,
`libSDL3.ini`, then the environment variable with the same name. SDL can reject
`SDL_SetHint` when an environment variable already exists; use
`SDL_SetHintWithPriority(..., SDL_HINT_OVERRIDE)` in that case.
The ini is read once on the first driver setting lookup and cached for the process.
The first file found wins as a whole: `SDL_RDP_INI` (application hint, else
environment), `libSDL3.ini` beside the loaded SDL3 shared library, then
`libSDL3.ini` in the current working directory. An unreadable explicit path fails
driver startup; missing default files are fine. `SDL_HINT_RDP_INI` names the path
hint; setting it inside the file does not select another file.

Each line is `NAME = value`, using environment names, e.g. `SDL_RDP_PORT = 33892`.
Leading/trailing blanks are trimmed; double quotes preserve inner blanks and allow `""`.
Blank lines and lines beginning with `#` or `;` (after trimming) are ignored.
`[section]` lines are ignored; duplicate keys use the last value.
Unknown keys and lines without `=` are skipped with a file/line warning; values are never logged.

File permissions can restrict access to `SDL_RDP_PASSWORD` in the ini, unlike
exposing it in the environment; keep the file readable only by the intended user.
The ini cannot select `SDL_VIDEO_DRIVER` or `SDL_AUDIO_DRIVER`: SDL core reads
those before the RDP driver runs. Set them through hints or the environment.

Settings include:
`SDL_RDP_PORT` (3389, 0 for ephemeral), `SDL_RDP_BIND` (0.0.0.0),
`SDL_RDP_CERT_DIR` (`$XDG_DATA_HOME/sdl-rdp` or `~/.local/share/sdl-rdp`), `SDL_RDP_WIDTH`, `SDL_RDP_HEIGHT` (1024x768),
`SDL_RDP_WAIT_FOR_CLIENT`, `SDL_RDP_BACKEND` (path of the backend library),
`SDL_RDP_AUDIO_LATENCY` (500 ms), `SDL_RDP_AUDIO_LEAD` (150 ms).
`SDL_RDP_VSYNC` defaults to `1`: surface updates wait up to 100 ms for the
client to acknowledge every frame except the latest present, allowing rendering
to overlap the latest frame's encoding, delivery and decoding. At most two frames
are in flight. Set it to `0` to return immediately.
`SDL_RDP_ASPECT` sets the picture's display aspect (for example `4:3`);
empty means square pixels. It can change live, and
`SDL_PROP_WINDOW_RDP_ASPECT_STRING` reports it on the window.
The window keeps the app's requested size; the RDP desktop is that picture,
with only pixel-aspect correction applied on the server. SDL's desktop mode is
the client's screen size after a client screen change and the picture size after
the application's own resize, including the windowed size restored on leaving
fullscreen. Borderless fullscreen fills the desktop mode; exclusive fullscreen
keeps its selected size. Client-side smart sizing can
stretch the picture to the client's screen. Resizes during client reactivation
are coalesced and applied when the client is active again.

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
and `--aspect 4:3` declares the picture's display aspect. F4 toggles fullscreen; F6 resizes the window to 1920x1080.
`--partial` submits only the animated strip between initial and exposed/resized full frames.

`SDL_RDP_CODEC` accepts `auto` (default), `remotefx`, `nscodec`, `planar`,
`raw`, `progressive`, and `avc420`. On legacy connections, auto selects RemoteFX, then NSCodec, then planar, then raw among
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
