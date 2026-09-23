# Building

Linux only for now. The tree needs a C++23 compiler (GCC 14 or newer), CMake,
ninja, Python 3 and conan 2; FreeRDP 3 with its development headers
(`freerdp3-dev` and `libwinpr3-dev` on Debian and Ubuntu) and OpenSSL from the
system; `nv-codec-headers` and gtest from conan. AVC420 needs an NVIDIA driver
with NVENC at runtime and is skipped without one. The build driver `buildutil`
and the `oxbox` utility library the backend uses are not published yet; until
they are, the tree builds only where a conan remote and a package index provide
them. `./buildutil build` builds everything, `./buildutil test` runs the gate,
and `./buildutil build --release` writes the release libraries under
`_build/<profile>/`.

The SDL patch touches CMake/build configuration, public hint/video headers, `src/SDL_hints*`, and audio/video/storage bootstrap files to register RDP drivers and hints.
It adds `src/{audio,storage}/rdp/` drivers; `src/video/SDL_video.c` preserves driver errors and reports initial refresh changes.
`src/render/SDL_render.c` updates simulated vsync from the live current mode for RDP only, including exclusive fullscreen.

# Consuming

`buildutil publish` packages `sdl-rdp` for conan. A project takes it with one
`Require` line and links the `SDL3` component; the backend is never linked,
only found next to it at runtime.

# Quality gate

Run `./buildutil test` and `./buildutil analyze` before committing.
`bin/lint-shape.py` checks non-blank file lines, class lines, member counts,
access/function/data ordering, and wholly-comment line percentages in `sources/`.
`bin/lint-shape.allow` records exact outstanding class measurements. A smaller
measurement or a removed violation makes its entry stale; remove or reduce the
entry in the same change. Never raise a limit; the allow list only shrinks.
Run `python3 bin/lint-shape.py --allow bin/lint-shape.allow` directly to inspect
shape failures. Run `bin/lint-clones.sh` for the clone gate (40 tokens, 5 lines).
Test fixtures retain protected data members so derived test bodies can use them.
The C driver is checked by `bin/lint-tidy-c.sh` using the GCC debug database.
It skips with code 77 and a reason if the database or clang-tidy is absent.
All three lint gates run under CTest when tests are built. Missing `npx` explicitly skips the
clone gate with exit code 77; it never passes silently.

The driver C files in `rdp/` are compiled directly, not copied into the SDL patch.
For bootstrap changes, extract two pristine copies of the pinned SDL archive,
apply `rdp-driver.patch` to one, edit it, then regenerate with `diff -ruN a b`.
