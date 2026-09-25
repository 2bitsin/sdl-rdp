# Building

Linux only for now. The tree needs a C++23 compiler (GCC 14 or newer), CMake,
ninja, Python 3 and conan 2. FreeRDP 3.32.0 (built from the `2bitsin/FreeRDP`
recipe), OpenSSL, `nv-codec-headers` and gtest come from conan; nothing FreeRDP
or OpenSSL is taken from the system. AVC420 needs an NVIDIA driver with NVENC at
runtime and is skipped without one. The build driver `buildutil`, the `oxbox`
utility library the backend uses and the `freerdp/3.32.0` package are not
published yet (`freerdp/3.32.0` is private on the project's conan remote); until they are,
the tree builds only where a conan remote and a package index provide them.
`./buildutil build` builds everything, `./buildutil test` runs the gate (Release,
parallel CTest), and `./buildutil build --release` writes the release libraries
under `_build/<profile>/`.

The SDL patch touches CMake/build configuration, public hint/video headers, `src/SDL_hints*`, and audio/video/storage bootstrap files to register RDP drivers and hints.
It adds `src/{audio,storage}/rdp/` drivers; `src/video/SDL_video.c` preserves driver errors and reports initial refresh changes.
`src/render/SDL_render.c` updates simulated vsync from the live current mode for RDP only, including exclusive fullscreen.

# Consuming

`buildutil publish` packages `sdl-rdp` for conan. A project takes it with one
`Require` line and links the `SDL3` component; the backend is never linked,
only found next to it at runtime.

# Quality gate

Run `./buildutil test` and `./buildutil analyze` before committing.
`tools/lint/shape.py` checks non-blank file lines, class lines, member counts,
access/function/data ordering, and wholly-comment line percentages in `sources/`;
function and lambda body lines (over 20 needs an allow entry, over 40 fails),
complexity, parameters and nesting, lines over 120 columns, compound contracts,
NOLINT lines and C files in `sources/`; and columns, body lines, parameters and
nesting in `tools/lint/*.py`.
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
The driver (`sources/sdl-rdp/SDL3/rdp`) is C++23 under clang-tidy like the backend;
its INI parser sits beside `settings.cpp`, its one user, and the backend's C ABI header is
the header-only module `sources/sdl-rdp/abi/`.
`tools/lint/` is a pytest suite that `buildutil test` runs after CTest:
`test_gate.py` runs every lint over the tree, the other files test the lints.
A lint whose tool is missing (`npx`, clang-format 20) fails with the reason
printed; the gate has no skips. `python3 tools/lint/includes.py` refuses an
include of another module's header unless the module links it.

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
