# Building

Linux only for now. The tree is C++26 (GCC 16 and Clang 20 are the compilers
it is built with) and needs CMake, ninja, Python 3 and conan 2. Every
dependency is public. The build driver is
[buildutil](https://github.com/2bitsin/buildutil) v0.96.0, installed with pipx
(an install into a virtual environment you activate yourself does not
bootstrap its toolchain yet, 2bitsin/buildutil#2).
`openssl/3.6.3`, `nv-codec-headers/13.0.19.0` (FFmpeg's NVENC headers), gtest
and google-benchmark come from conancenter. Two packages are built once into
this checkout's conan home, `_conanhome/`, after which the `Require` lines in
`sources/CMakeLists.txt` resolve them: `oxbox/0.33.1.364`, the utility library
the backend uses, from tag `v0.33.1` of
[2bitsin/oxbox](https://github.com/2bitsin/oxbox) (the pin names the build
number its published package carries, and the local build takes the same
number); and `freerdp/3.32.0`, FreeRDP 3.32.0 with the patches this project
needs, from tag `3.32.0-sdl-rdp.1` of
[2bitsin/FreeRDP](https://github.com/2bitsin/FreeRDP):

```sh
pipx install git+https://github.com/2bitsin/buildutil@v0.96.0
export CONAN_HOME="$PWD/_conanhome"
(git clone -b v0.33.1 https://github.com/2bitsin/oxbox.git ../oxbox && cd ../oxbox &&
  ./buildutil publish --conan-home "$CONAN_HOME" --no-upload --release --version 0.33.1.364)
(git clone -b 3.32.0-sdl-rdp.1 https://github.com/2bitsin/FreeRDP.git ../FreeRDP && cd ../FreeRDP &&
  conan create . --build=missing)
```

Nothing FreeRDP or OpenSSL is taken from the system at build time. AVC420
needs an NVIDIA driver with NVENC at runtime and is skipped without one.
`./buildutil build` builds everything, `./buildutil test` runs the gate (Release,
parallel CTest), and `./buildutil build --release` installs the release build
into `_install/` at each module's source-relative path: `sdl-rdp/libSDL3.so`,
`sdl-rdp/libbackend.so`, `sample`, and the headers under `include/`.

The SDL patch touches CMake/build configuration, public hint/video headers, `src/SDL_hints*`, and audio/video/storage bootstrap files to register RDP drivers and hints.
It adds `src/{audio,storage}/rdp/` drivers; `src/video/SDL_video.c` preserves driver errors and reports initial refresh changes.
`src/render/SDL_render.c` updates simulated vsync from the live current mode for RDP only, including exclusive fullscreen.

# Consuming

`buildutil publish` packages `sdl-rdp` for conan. A project takes it with one
`Require` line and links the `SDL3` component. The backend is a separate shared
library that is never linked: only when the `rdp` driver is selected does it
dlopen `libbackend.so` by that leaf name, which libSDL3's run path resolves
beside `libSDL3.so`, or the path `SDL_RDP_BACKEND` or the settings file's
`backend` names. An application that ships libSDL3
ships `libbackend.so` beside it. The backend links the fork's
`libfreerdp3.so.3`, `libfreerdp-server3.so.3` and `libwinpr3.so.3` and
OpenSSL 3's `libssl.so.3` and `libcrypto.so.3`, and carries no run path to
them: the install tree and the release archive do not include them, so the
machine that runs the application provides them on the loader's path, the
fork's build (`lib/` of its conan package) rather than a distribution's
FreeRDP 3, whose older releases lack the fork's fixes.

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
and the backend's C ABI header is the header-only module `sources/sdl-rdp/abi/`.
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
