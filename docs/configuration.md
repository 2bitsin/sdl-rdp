# Selecting it

Opt in only: probing never picks the driver and no listener opens unasked.

    SDL_VIDEO_DRIVER=rdp        # from the shell, any SDL program
    SDL_HINT_VIDEO_DRIVER       # from code, before SDL_Init: an app's --rdp flag

Driver settings use these sources in order: an application hint set through SDL,
the settings file, then the environment variable named like the hint. SDL can reject
`SDL_SetHint` when an environment variable already exists; use
`SDL_SetHintWithPriority(..., SDL_HINT_OVERRIDE)` in that case.
The file is read when the driver starts; `SDL_Quit` forgets it and the next `SDL_Init` reads it again.

The settings file is named after the loaded SDL library with its extension replaced:
`libSDL3.yaml` beside `libSDL3.so` or `libSDL3.dylib`, `SDL3.yaml` beside `SDL3.dll`.
Any format oxbox serialization reads by extension works (`.yaml`, `.yml`, `.json`, `.xml`,
`.bsx`, `.bsp` today); the extension picks the format. The first file found wins as a whole:
`SDL_RDP_SETTINGS` (application hint, else environment) names one file of any supported
extension, then the file beside the library, then the file in the current working directory.
Two files with the same name and different extensions at one location fail driver startup,
naming both; so does an unreadable explicit path. Missing default files are fine.

YAML is the human format. Keys are the setting names in lower case without the `SDL_RDP_`
prefix; a key left out, or given no value, is absent and the environment answers it:

```yaml
port: 33892                 # 0 for ephemeral
bind: 127.0.0.1
cert_dir: /home/me/.local/share/sdl-rdp
width: 1280
height: 800
refresh: auto-client        # or a rate in Hz
aspect: 4:3
codec: planar
avc_bitrate: 8000           # kbit/s
vsync: false
wait_for_client: false
audio_latency: 500          # ms
audio_lead: 150             # ms
user: me
password: secret
domain: example
auth: nla
```

Every value has its real type: numbers are whole numbers within the setting's range,
`vsync` and `wait_for_client` are `true` or `false`, `codec` and `auth` are one of their
names, `refresh` is a mode name or a whole number of hertz, and `aspect` is `N:D` with two
positive whole numbers. An unknown key, a value of the wrong type or outside its range, and
a file the format cannot parse fail `SDL_Init` once, with the file and the cause in
`SDL_GetError()`; values are never logged. A file from an earlier release that nests the
settings under a `backend:` key fails the same way, with `unknown key 'backend'`; the keys
are top level. Hints and environment variables keep SDL's text forms (`SDL_RDP_VSYNC=1`).

File permissions can restrict access to `password` in the file, unlike
exposing it in the environment; keep the file readable only by the intended user.
The file cannot select `SDL_VIDEO_DRIVER` or `SDL_AUDIO_DRIVER`: SDL core reads
those before the RDP driver runs. Set them through hints or the environment.

Settings include:
`SDL_RDP_PORT` (3389, 0 for ephemeral), `SDL_RDP_BIND` (0.0.0.0),
`SDL_RDP_CERT_DIR` (`$XDG_DATA_HOME/sdl-rdp` or `~/.local/share/sdl-rdp`), `SDL_RDP_WIDTH`,
`SDL_RDP_HEIGHT` (1024x768), `SDL_RDP_REFRESH` (integer Hz, `auto-client`, `auto-client-average`,
or `auto-sender`; default 60, set before video initialization),
`SDL_RDP_WAIT_FOR_CLIENT`,
`SDL_RDP_AUDIO_LATENCY` (500 ms), `SDL_RDP_AUDIO_LEAD` (150 ms).
`SDL_RDP_VSYNC` defaults to `0`: surface updates return as soon as the driver takes the frame,
and SDL renderer vsync uses the display refresh rate for timed pacing.
Set it to `1` to wait up to 100 ms for client acknowledgements, allowing the latest present to remain in flight.
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
and these sizes at the declared `SDL_RDP_REFRESH` rate:

320x200, 320x240, 320x256, 400x300, 512x384, 640x350, 640x400, 640x480,
720x400, 720x480, 800x600, 1024x768, 1280x720, 1280x800, 1920x1080,
1920x1200, 2560x1440, 3840x2160.

Sample flags: `--fullscreen --mode 320x200` selects exclusive fullscreen,
`--fullscreen` selects borderless, `--size 640x480` sets the windowed size,
and `--aspect 4:3` declares the picture's display aspect. F4 toggles fullscreen; F6 resizes the window to 1920x1080.
`--partial` submits only the animated strip between initial and exposed/resized full frames.

`SDL_RDP_CODEC` accepts `auto` (default), `remotefx`, `nscodec`, `planar`,
`raw`, `progressive`, and `avc420`. On legacy connections, auto selects
RemoteFX, then NSCodec, then planar, then raw among negotiated codecs. An
unsupported explicit preference uses that same fallback.
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
reports the client's screen. `SDL_RDP_REFRESH` accepts an application hint, environment variable or settings
file `refresh` with the precedence above; an unknown value in the file fails `SDL_Init`, and
one in a hint or the environment fails video initialization and logs one SDL error. The
desktop mode declares the ceiling (60 Hz for adaptive modes), and the current mode exposes the
effective rate used by simulated vsync and AVC.

- Integer Hz keeps a fixed declared rate, independent of transport or client timing.
  It gives predictable pacing but cannot adapt to a slow link.
- `auto-client` uses send-to-ack latency, stepping down 10 Hz above two declared
  frame intervals and up 10 Hz below one, bounded by 10 Hz and the declared ceiling.
  It recovers independently of the app's present frequency, but includes client
  processing and acknowledgement delay as well as transport latency; Doom with
  xfreerdp3 on a LAN settled at 10 presents/s in both client-based modes.
- `auto-client-average` uses the original 0.8/0.2 moving average of acknowledgement
  gaps with its original 5% publication threshold, bounded by 10 Hz and the
  declared ceiling with reconnect gaps excluded.
  Acknowledgements arrive only as fast as the app presents, so this mode can settle
  at its 10 Hz floor (Doom with xfreerdp3 on a LAN: 10 presents/s, as with
  `auto-client`) and only a mode or size change restarts it at the ceiling;
  matching the client's rate trades away automatic recovery.
- `auto-sender` samples Linux TCP_INFO and SIOCOUTQ after each written frame,
  stepping down 10 Hz when queued bytes or unacknowledged segments exceed one
  frame, also stepping down on blocked writes at most once per current frame
  interval, and up 10 Hz after a completed write into an empty send buffer,
  within the same bounds.
  It measures socket pressure independently of RDP acknowledgements but cannot
  measure client decoding speed; unavailable TCP measurements disable queue-based
  adaptation and produce one warning per connection, while blocked writes still
  step down.

Every adaptive estimate restarts at the ceiling on picture or mode changes and
surface recreation, excluding old acknowledgements and reconnect drain time.
`SDL_RDP_VSYNC=1` still blocks present on acknowledgement and composes with every
refresh mode. `SDL_RDP_TRACE=1` includes `outq` bytes, `unacked` segments and
`tcp_rtt` microseconds after writes; disconnect statistics include mean and
maximum send-buffer occupancy.
The current mode keeps the selected fullscreen size in exclusive fullscreen.
Details are properties (bound port on the display, client name on the window). A failed open
is in `SDL_GetError()`; the driver's diagnostics go to `SDL_Log` in the video category.

`SDL_RDP_TRACE=1` prints a wall-clock stamped line per input event, audio block,
confirmation, frame and acknowledgement through `SDL_Log` at info level, for correlating with a client-side recording. Timestamps are integer
milliseconds since the epoch. The variable is read once when the backend opens;
tracing is disabled by default. Lines also include presentation, connection and
audio gate transitions, with audio RMS and peak levels on the S16 scale.
