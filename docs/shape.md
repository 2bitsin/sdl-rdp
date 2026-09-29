# Shape

One buildutil project. `./buildutil build`, `./buildutil test`,
`./buildutil publish` at the root.

Every package module lives under `sources/sdl-rdp/`, the package root folder, and the sample
beside it in `sources/sample/`; each installs at its source-relative path:
`<prefix>/sdl-rdp/libSDL3.so` and `<prefix>/sample`.

`sources/sdl-rdp/SDL3/versions.toml` names the SDL release the build uses (`build`)
and the sha256 of every supported release; another pinned release is selected
by editing `build`. Each
version keeps its archive, patched tree and configure cache under
`_build/generated/sdl-rdp-SDL3/<version>/`.

- `sources/sdl-rdp/SDL3/` builds `libSDL3.so`, the library applications link.
  `rdp/` is the driver, C++26 with `extern "C"` only at SDL's bootstrap
  tables and no FreeRDP include; it links the backend modules and composes
  their classes directly. `rdp/driver.hpp` is the `Driver` class holding the
  composition root, the `session` module's `Backend` (one
  listener, its session, event queue, presenter, audio output, clipboard and
  drive files), built from a `configuration::Setup` record that `settings/`
  produces from the settings file and hints. The driver reads the backend's
  `EventQueue` of `link::Event`, implements `diagnostics::LogSink` over
  `SDL_LogMessage` (`rdp/log-relay.*`) and `configuration::CredentialCheck` as
  `CredentialRelay` over the display properties. `rdp/sdl/` (`Boundary`,
  `ErrorRoutes`, `CheckedAcquisition`, `PointerState`, the SDL RAII
  `resources`, `internals`) is the driver's side of SDL's own API;
  `Boundary` catches the backend's exceptions once at each SDL callback slot
  into `SDL_SetError`. `rdp-driver.patch` registers
  the driver in SDL's build and bootstrap list. `configure.py` is buildutil's
  per-module hook: it downloads the selected SDL release into the generated
  folder once, applies the patch there, runs SDL's own CMake configure (never
  its build) to learn the file list, defines and `SDL_build_config.h` for the
  build's target and toolchain, and hands those sources to buildutil, which compiles
  them, the driver and the backend modules into one library whose dynamic
  section names libfreerdp-server3, libfreerdp3, libwinpr3, libssl and
  libcrypto (`readelf -d`; the NVENC loader is resolved at run time). Nothing SDL
  lands in the source tree. `tools/exports/` checks the library's dynamic
  table against the list SDL publishes.
- The rest of `sources/sdl-rdp/` holds the backend's static modules by functionality:
  `utilities`, `freerdp-facade`, `diagnostics`, `picture`, `configuration`,
  `link`, then `auth`, `video`, `audio`,
  `input`, `clipboard`, `drive`, then `peer`, then `session`, and `settings`
  (the settings file and hints into a `configuration::Setup`); each links only
  modules before it, and `SDL3` links them. Nothing in them constructs a session,
  listener or FreeRDP object until `SDL_Init` selects the `rdp` driver.
  Unit tests sit beside the code they test. The rigs are the test-lane modules
  `headless-client.test` (the headless FreeRDP client and backend fixtures) and
  `sample-gate.test` (the sample and driver fixtures), which never ship.
- `sources/sdl-rdp/integration/` is one module holding every test that drives the backend
  or the sample through a rig, and the benches; it links both rigs as `TEST` and `BENCH`
  dependencies. Its partitions are `*.test/` subtrees (`auth.test`, `audio.test`,
  `video.test`, `session.test`, `drive.test`, `clipboard.test`, `sample.test`,
  `tls-race.test`, `allocations.test`) and `*.bench/` subtrees. The backend is
  exercised there (a headless FreeRDP client connects, frames and input
  round-trip). Nothing of it ships.
- `sources/sample/` is the sample: draws a pattern, prints every
  SDL event as one line.
- `test_package/` consumes the published package the way a downstream
  project does: links the `SDL3` component, starts the `rdp` driver on an
  ephemeral port, and proves the driver runs from the package alone.

`tools/lint/shape.py` measures every `.hpp` under `sources/` twice:

- `classes with member functions`: at most one class, nested ones counted with
  their owner, declares a member function that is not defaulted or deleted.
- `bodies in header`: none, unless the class or the function is a template or
  the function is constexpr or consteval; every other body is in the `.cpp`.

WLog has no callback user pointer, so `diagnostics/logging.cpp` keeps one
process-lifetime `static Routing` inside `libSDL3.so`: the callback lock, the
installation flag and the per-thread peer filters. The newest backend supplies
the sink; destroying it clears routing without restoring an older backend, so
after `SDL_Quit` the object stays installed and routes nowhere (#76). Route
changes and callback delivery share the same lock.
