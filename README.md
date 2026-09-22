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
  server behind a nine-function C ABI (`sdl-rdp-backend.h`), with its gtest
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
`SDL_RDP_CERT_DIR` (`_rdp`), `SDL_RDP_WIDTH`, `SDL_RDP_HEIGHT` (1024x768),
`SDL_RDP_WAIT_FOR_CLIENT`, `SDL_RDP_BACKEND` (path of the backend library).
Session facts arrive as native SDL events: a client attaching is
EXPOSED + FOCUS_GAINED, leaving is OCCLUDED + FOCUS_LOST, the display mode
follows the client's desktop; details are properties (bound port on the
display, client name on the window). A failed open is in `SDL_GetError()`,
backend diagnostics go to `SDL_Log`.

## Consuming

`buildutil publish` pushes `sdl-rdp` to the site conan remote. A project takes
it with one `Require` line and links the `SDL3` component; the backend is never
linked, only found next to it at runtime.
