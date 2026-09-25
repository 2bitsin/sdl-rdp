# Shape

One buildutil project. `./buildutil build`, `./buildutil test`,
`./buildutil publish` at the root.

Every package module lives under `sources/sdl-rdp/`, the package root folder, and the sample
beside it in `sources/sample/`; each installs at its source-relative path:
`<prefix>/sdl-rdp/libSDL3.so`, `<prefix>/sdl-rdp/libbackend.so` and `<prefix>/sample`.

`sources/sdl-rdp/SDL3/versions.toml` names the SDL release the build uses (`build`)
and the sha256 of every supported release; another pinned release is selected
by editing `build`. Each
version keeps its archive, patched tree and configure cache under
`_build/generated/sdl-rdp-SDL3/<version>/`.

- `sources/sdl-rdp/SDL3/` builds `libSDL3.so`, the library applications link.
  `rdp/` is the driver, C++23 with `extern "C"` only at SDL's bootstrap
  tables (since #22) and no FreeRDP dependency. `rdp-driver.patch` registers
  it in SDL's build and bootstrap list. `configure.py` is buildutil's
  per-module hook: it downloads the selected SDL release into the generated
  folder once, applies the patch there, runs SDL's own CMake configure (never
  its build) to learn the file list, defines and `SDL_build_config.h` of a
  headless Linux build, and hands those sources to buildutil, which compiles
  them and the driver into one library. Nothing SDL lands in the source tree.
- `sources/sdl-rdp/abi/` is the header-only module holding the backend's C ABI,
  `<sdl-rdp/abi/backend.h>`; every module that includes it links it.
- The rest of `sources/sdl-rdp/` holds the backend's static modules by functionality:
  `utilities`, `freerdp-facade`, `diagnostics`, `configuration`, `picture`,
  `link`, then `auth`, `video`, `audio`,
  `input`, `clipboard`, `drive`, then `peer`, then `session`; each links only modules
  before it. Unit tests sit beside the code they test. The rigs are the test-lane modules
  `headless-client.test` (the headless FreeRDP client and backend fixtures) and
  `sample-gate.test` (the sample and driver fixtures), which never ship.
- `sources/sdl-rdp/integration/` is one module holding every test that drives the backend
  or the sample through a rig, and the benches; it links both rigs as `TEST` and `BENCH`
  dependencies. Its partitions are `*.test/` subtrees (`auth.test`, `audio.test`,
  `video.test`, `session.test`, `drive.test`, `clipboard.test`, `sample.test`,
  `tls-race.test`, `allocations.test`) and `*.bench/` subtrees. Nothing of it ships.
- `sources/sdl-rdp/backend/` builds `libbackend.so` from those
  modules: the FreeRDP 3 server behind a versioned C ABI (`abi`),
  exercised by the integration suites (a headless FreeRDP client connects, frames and input
  round-trip). The driver dlopens it by name only when the `rdp` driver is
  selected, so SDL stays free of FreeRDP and a program runs without the
  backend installed.
- `sources/sample/` is the sample: draws a pattern, prints every
  SDL event as one line.
- `test_package/` consumes the published package the way a downstream
  project does: links the `SDL3` component, starts the `rdp` driver on an
  ephemeral port, and proves the backend resolves from the package.

`tools/lint/shape.py` measures every `.hpp` under `sources/` twice:

- `classes with member functions`: at most one class, nested ones counted with
  their owner, declares a member function that is not defaulted or deleted.
- `bodies in header`: none, unless the class or the function is a template or
  the function is constexpr or consteval; every other body is in the `.cpp`.

`sdlrdp_last_error()` reports the calling thread's most recent error; successful
calls do not clear it. Each handle owns its per-thread error slots. A thread-local
cursor retains the most recently published slot after handle close. Failed opens
and null-handle calls publish a cursor-owned error instead.

WLog has no callback user pointer. `LogRoute` owns the process-lifetime routing
object, its callback lock, installation flag, and per-thread peer filters. The
newest handle supplies the callback; closing it clears routing without restoring
an older handle. Route changes and callback delivery share the same lock.
