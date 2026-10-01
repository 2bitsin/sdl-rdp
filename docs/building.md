# Building

The tree is developed, built and tested on Linux. It is C++26 (GCC 16 and
Clang 20 are the compilers it is built with) and needs CMake, ninja, Python 3
and conan 2. Every dependency is public. The Windows and macOS binaries are
cross-built on Linux, in buildutil's `wine-msvc` and `osxcross` lanes (Release
archives, below). The build driver is
[buildutil](https://github.com/2bitsin/buildutil) v0.99.0, installed with pipx
(an install into a virtual environment you activate yourself does not
bootstrap its toolchain yet, 2bitsin/buildutil#2).
`openssl/3.6.3`, `nv-codec-headers/13.0.19.0` (FFmpeg's NVENC headers), gtest
and google-benchmark come from conancenter. Two packages are put into this
checkout's conan home, `_conanhome/`, after which the `Require` lines in
`sources/CMakeLists.txt` resolve them and the first `./buildutil build`
compiles both inside the cache for this project's dependency graph:
`oxbox/0.37.0.384`, the utility library the backend uses, from tag `v0.37.0`
of [2bitsin/oxbox](https://github.com/2bitsin/oxbox) (the pin names the build
number its published package carries, and the local build takes the same
number; `--bake-buildutil` ships the driver inside the package so this
project's build can rebuild it, because FreeRDP takes OpenSSL shared and that
gives oxbox a different package id from the one a standalone build produces);
and `freerdp/3.32.0-sdl-rdp.3`, FreeRDP 3.32.0 with the patches this project needs, from
tag `3.32.0-sdl-rdp.3` of [2bitsin/FreeRDP](https://github.com/2bitsin/FreeRDP),
exported as a recipe only (a fresh conan home has no profile for
`conan create`; the project's build carries the right one):

```sh
pipx install git+https://github.com/2bitsin/buildutil@v0.99.0
export CONAN_HOME="$PWD/_conanhome"
(git clone -b v0.37.0 https://github.com/2bitsin/oxbox.git ../oxbox && cd ../oxbox &&
  ./buildutil publish --conan-home "$CONAN_HOME" --no-upload --release --version 0.37.0.384 --bake-buildutil)
(git clone -b 3.32.0-sdl-rdp.3 https://github.com/2bitsin/FreeRDP.git ../FreeRDP && cd ../FreeRDP &&
  conan export .)
./buildutil build --watchdog-budget 3600
```

The first build compiles FreeRDP and oxbox in the conan cache, which is why
it carries a watchdog budget: buildutil's default of 180 s assumes prebuilt
dependencies. Later builds do not need it; `./buildutil test` takes the same
flag on a machine that needs more than three minutes for the gate.
Nothing FreeRDP or OpenSSL is taken from the system at build time. AVC420
needs an NVIDIA driver with NVENC at runtime and is skipped without one.
`./buildutil build` builds everything, `./buildutil test` runs the gate (Release,
parallel CTest), and `./buildutil build --release` installs the release build
into `_install/` at each module's source-relative path: `sdl-rdp/libSDL3.so`,
`sample`, and the headers under `include/`.

The SDL patch touches CMake/build configuration, public hint/video headers,
`src/SDL_hints*`, and audio/video/storage bootstrap files to register RDP drivers and
hints. It adds `src/{audio,storage}/rdp/` drivers; `src/video/SDL_video.c` preserves driver
errors and reports initial refresh changes. `src/render/SDL_render.c` updates simulated
vsync from the live current mode for RDP only, including exclusive fullscreen.

# Consuming

`buildutil publish` packages `sdl-rdp` for conan. A project takes it with one
`Require` line and links the `SDL3` component. The backend is compiled into
`libSDL3.so`: a consumer compiles against the SDL3 headers and links that one
library, and nothing constructs a session, listener or FreeRDP object until the
`rdp` driver is selected. At load time every program linking this `libSDL3.so`
must find its five `NEEDED` libraries whether or not it selects `rdp`: the
fork's `libfreerdp3.so.3`, `libfreerdp-server3.so.3` and `libwinpr3.so.3` and
OpenSSL 3's `libssl.so.3` and `libcrypto.so.3`. `libSDL3.so` carries no run
path to them and the install tree and the release archive do not include them,
so the machine that runs the application provides them on the loader's path,
from the fork's build (`lib/` of its conan package) rather than a
distribution's FreeRDP 3, whose older releases lack the fork's fixes.

# Release archives

Each [release](https://github.com/2bitsin/sdl-rdp/releases) carries three
archives and one `SHA256SUMS` over them. Each unpacks into
`sdl-rdp-<version>-<target>/` and holds a Release build with contracts
compiled out, SDL's public headers in `include/SDL3/`, `LICENSE`, `README.md`,
`docs/`, and in `share/licenses/<package>/` the licence of SDL and of every
package whose code is inside a shipped binary, statically linked or carried
beside the library, oxbox's included.

`sdl-rdp-<version>-linux-x86_64.tar.gz` holds `sdl-rdp/libSDL3.so*` and
`sample`, which finds the library through its run path. The machine provides
the fork's FreeRDP and OpenSSL 3 (Consuming, above), glibc and the libstdc++ of
GCC 16. Point the loader at the `lib/` directories of the two (the paths
below are examples) and run the sample:

```sh
VERSION=0.4.0
tar xzf sdl-rdp-$VERSION-linux-x86_64.tar.gz
cd sdl-rdp-$VERSION-linux-x86_64
export LD_LIBRARY_PATH=/opt/freerdp-sdl-rdp/lib:/opt/openssl-3/lib
SDL_VIDEO_DRIVER=rdp SDL_RDP_PORT=3389 ./sample
```

`sdl-rdp-<version>-windows-x86_64.zip` holds everything that runs in
`sdl-rdp/`: `SDL3.dll`, its import library `SDL3.lib`, `sample.exe`, and the
DLLs `SDL3.dll` loads, the fork's `freerdp3.dll`, `freerdp-server3.dll` and
`winpr3.dll` and OpenSSL's `libssl-3-x64.dll` and `libcrypto-3-x64.dll`.
OpenSSL reads its configuration from `C:\Program Files\Common Files\SSL`, which
only an administrator can create. Windows has no run path: the loader
looks in the program's own directory, so a program that links `SDL3.dll`
ships these DLLs beside its `.exe`, as `sample.exe` does. The machine
provides Windows' own DLLs (Windows 10 or later, for the universal C runtime)
and the Microsoft Visual C++ runtime (`vcruntime140.dll`,
`vcruntime140_1.dll`, `msvcp140.dll`, `msvcp140_1.dll`,
`msvcp140_atomic_wait.dll`): install the latest Visual C++ Redistributable
for x64 from Microsoft, since the build uses the MSVC 14.51 toolset and a
program needs a runtime at least that new. The archive does not carry it:
Microsoft services the redistributable through Windows Update, and a copy
beside the program would not be.

`sdl-rdp-<version>-macos-arm64.tar.gz` holds `sdl-rdp/libSDL3.0.dylib` (with
the `libSDL3.dylib` link), `sample`, and the five dylibs the library loads,
each under its install name with its links: `libfreerdp3.3.dylib`,
`libfreerdp-server3.3.dylib` and `libwinpr3.3.dylib` (the fork's, as
`.3.32.0.dylib` files) and OpenSSL's `libssl.3.dylib` and `libcrypto.3.dylib`,
which reads its configuration from `/private/etc/ssl`. The library and the dylibs are
named `@rpath/<name>`; `sample` carries `@loader_path/sdl-rdp` as its run
path, and a program that links `libSDL3.0.dylib` puts the directory holding
all six on its own. The machine provides only macOS's libraries and
frameworks, from macOS 26 on: every binary is built for arm64 with
`minos 26.0`. AVC420 needs NVENC and is never offered on a Mac. The binaries
carry the linker's ad-hoc signature and are not notarised, so Gatekeeper
refuses a downloaded copy until its quarantine attribute is removed:

```sh
VERSION=0.4.0
tar xzf sdl-rdp-$VERSION-macos-arm64.tar.gz
xattr -dr com.apple.quarantine sdl-rdp-$VERSION-macos-arm64
cd sdl-rdp-$VERSION-macos-arm64
SDL_VIDEO_DRIVER=rdp SDL_RDP_PORT=3389 ./sample
```

With that, the sample listens on port 3389 of every interface. Connect with an
RDP client: Windows App on the same Mac or another machine, mstsc, or
`xfreerdp` (`xfreerdp3` on Debian and Ubuntu) with the Mac's address in `MAC`:

```sh
MAC=192.0.2.10
xfreerdp /v:$MAC:3389 /cert:ignore
```

The window is the sample's
picture, a near-black background with one green square, it answers keys and
the mouse, and Escape ends it. That is the check a Mac build gets, since CI
cannot run what it cross-builds; `docs/authentication.md` covers credentials.

# Quality gate

Run `./buildutil test` and `./buildutil analyze` before committing.
`tools/lint/shape.py` checks non-blank file lines, class lines, member counts,
access/function/data ordering, and wholly-comment line percentages in `sources/`;
function and lambda body lines (over 20 needs an allow entry, over 40 fails),
complexity, parameters and nesting, lines over 120 columns, compound contracts,
NOLINT lines and C files in `sources/`; and columns, body lines, parameters and
nesting in `tools/*/*.py`.
`tools/lint/shape.allow` records exact outstanding measurements, each with the one
round that removes it as a required `# reason`; paths are relative to `sources/`
(`.` is the whole tree), the lint prints each finding in exactly this form so a
report line pastes in unchanged, the file is data and exempt from the 120-column
rule, and a body over 40 lines cannot be listed. A smaller
measurement or a removed violation makes its entry stale; remove or reduce the
entry in the same change. A function entry is keyed by the line of its parameter
list, so moving the definition makes its entry stale and the change re-measures
it. Never raise a limit; the allow list only shrinks. A brace opened or closed by
a macro is not seen by the lint; a file it leaves unbalanced is reported as
`unbalanced braces`.
Run `python3 tools/lint/shape.py --allow tools/lint/shape.allow` directly to
inspect shape failures, `python3 tools/lint/clones.py` for the clone gate (jscpd
4.0.5, 40 tokens, 5 lines), `python3 tools/lint/format.py` to format the tree
(`--check` to verify) and `python3 tools/lint/cmake.py` for the CMake vocabulary.
Test fixtures retain protected data members so derived test bodies can use them.
The driver (`sources/sdl-rdp/SDL3/rdp`) is C++26 under clang-tidy like the backend;
its settings file is the reflected record in `sources/sdl-rdp/settings/`, read through oxbox serialization,
and the backend is built from the `Setup` record in `sources/sdl-rdp/configuration/`.
`tools/lint/` is a pytest suite that `buildutil test` runs after CTest:
`test_gate.py` runs every lint over the tree, the other files test the lints.
A lint whose tool is missing (`npx`, clang-format 20) fails with the reason
printed; the gate has no skips. `python3 tools/lint/includes.py` refuses an
include of another module's header unless the module links it.
`tools/lint/casts.py` refuses a functional or C-style cast to a scalar type
(ES.48, ES.49): `Narrowed<T>(x)`, `T{ x }` or `static_cast<T>(x)` instead, and
`tools/lint/reserved.py` refuses an `_Upper` identifier ([lex.name]) but for the
names oxbox, NVENC and POSIX chose.

The driver sources in `rdp/` are compiled directly, not copied into the SDL patch.
For bootstrap changes, extract two pristine copies of the pinned SDL archive,
apply `rdp-driver.patch` to one, edit it, then regenerate with `diff -ruN a b`.

What measures the box's real-time scheduling (input and clipboard p95 under
tight video, PCM clock and cadence, present latency, graphics cost) is
google-benchmark in the integration module's bench lane,
`sources/sdl-rdp/integration/{audio,video,sample}.bench/` with the adapter in
`support.bench/`; their behaviour halves stay in the gate, which never runs the
bench lane. The allocation ratchets in `sources/sdl-rdp/integration/allocations.test/`
count deterministically and are gate tests. `./buildutil bench` builds and runs
the benchmarks.
