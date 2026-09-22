# sdl-rdp

SDL3 with an `rdp` video driver. An unmodified SDL program on a headless
machine runs with its window served over RDP, so a Windows or Mac RDP
client is its display and its keyboard and mouse.

## Shape

- `sdl/` — the driver, `src/video/rdp/`, as ordinary source files the
  conan recipe copies into the upstream SDL tree at the pinned tag, plus
  a short patch series that registers the driver in the bootstrap list
  and the CMake build. Built with every desktop backend off (no X11,
  Wayland, EGL, OpenGL, Vulkan, ALSA, PulseAudio, dbus, udev), which is
  also the configuration that builds on a headless box.
- `backend/` — `libsdl-rdp-backend`, a shared library with a five-function
  C ABI (open, present, poll input, close, version) that wraps the FreeRDP 3
  server: listener, peer lifecycle, TLS with a generated certificate,
  frame push, input callbacks. FreeRDP is linked statically inside it.
  The driver dlopens this library only when selected; a missing file
  means the driver reports unavailable and SDL continues to the next one.
- `test/` — the gate: an unmodified SDL sample built against this SDL,
  started on the box, connected to by a headless FreeRDP client; the
  received frame must equal what the sample drew, and injected input must
  arrive as SDL events.

## Selecting it

Opt in only; the driver sits after `dummy` in SDL's bootstrap list, so
probing never picks it and no listener opens unasked.

    SDL_VIDEO_DRIVER=rdp        # from the shell, any SDL program
    SDL_HINT_VIDEO_DRIVER       # from code, before SDL_Init: an app's --rdp flag

Driver settings are hints with environment variables of the same name:
`SDL_RDP_PORT` (3389), `SDL_RDP_BIND` (0.0.0.0), `SDL_RDP_CERT_DIR`,
`SDL_RDP_WAIT_FOR_CLIENT`.

## Packages

Two conan packages on the project's conan remote: `sdl` under this project's channel,
so a consumer switches by editing the version in its `Require` line, and
`sdl-rdp-backend`. FreeRDP has no conan recipe anywhere; the backend
recipe builds it from the upstream tag with servers on, clients off,
codecs and channels trimmed.

## Origin

Design settled 2026-09-22 in the emuex20260731 session; the first
FreeRDP server wrapper was written there and moves here as the backend.
