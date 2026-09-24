# Building

Linux only for now. The tree needs a C++23 compiler (GCC 14 or newer), CMake,
ninja, Python 3 and conan 2; FreeRDP 3 with its development headers
(`freerdp3-dev` and `libwinpr3-dev` on Debian and Ubuntu) and OpenSSL from the
system; `nv-codec-headers` and gtest from conan. AVC420 needs an NVIDIA driver
with NVENC at runtime and is skipped without one. The build driver `buildutil`
and the `oxbox` utility library the backend uses are not published yet; until
they are, the tree builds only where a conan remote and a package index provide
them. `./buildutil build` builds everything, `./buildutil test --parallel` runs
the gate, and `./buildutil build --release` writes the release libraries under
`_build/<profile>/`.

The SDL patch touches CMake/build configuration, public hint/video headers, `src/SDL_hints*`, and audio/video/storage bootstrap files to register RDP drivers and hints.
It adds `src/{audio,storage}/rdp/` drivers; `src/video/SDL_video.c` preserves driver errors and reports initial refresh changes.
`src/render/SDL_render.c` updates simulated vsync from the live current mode for RDP only, including exclusive fullscreen.

# Consuming

`buildutil publish` packages `sdl-rdp` for conan. A project takes it with one
`Require` line and links the `SDL3` component; the backend is never linked,
only found next to it at runtime.

# Quality gate

Run `./buildutil test --parallel` and `./buildutil analyze` before committing.
`bin/lint-shape.py` checks non-blank file lines, class lines, member counts,
access/function/data ordering, and wholly-comment line percentages in `sources/`;
function and lambda body lines (over 20 needs an allow entry, over 40 fails),
complexity, parameters and nesting, lines over 120 columns, compound contracts,
NOLINT lines and C files in `sources/`; and columns, body lines, parameters and
nesting in `bin/*.py`.
`bin/lint-shape.allow` records exact outstanding measurements, each with the one
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
Run `python3 bin/lint-shape.py --allow bin/lint-shape.allow` directly to inspect
shape failures. Run `bin/lint-clones.sh` for the clone gate (40 tokens, 5 lines).
Test fixtures retain protected data members so derived test bodies can use them.
The C driver (`sources/SDL3.so/rdp`) is SDL's own style and is outside
clang-tidy; it is glue only, and new code goes on the C++ side.
Both lint gates and the lint's own tests run under CTest when tests are built.
Missing `npx` skips the clone gate, and missing pytest skips `shape-lint-tests`,
with exit code 77 and the reason printed; neither passes silently.

The driver C files in `rdp/` are compiled directly, not copied into the SDL patch.
For bootstrap changes, extract two pristine copies of the pinned SDL archive,
apply `rdp-driver.patch` to one, edit it, then regenerate with `diff -ruN a b`.

The gate excludes the `bench` label, the tests that measure the box's real-time
scheduling (input and clipboard p95 under tight video, PCM clock and cadence,
present latency); their behaviour halves stay in the gate.
`./buildutil test --parallel bench` runs the benchmarks. Buildutil has no project
setting for parallel tests, so every gate command carries `--parallel`.
